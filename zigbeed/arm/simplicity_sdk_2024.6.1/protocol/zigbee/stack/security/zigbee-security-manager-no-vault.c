/***************************************************************************//**
 * @file
 * @brief Implementation of security-related APIs, including key storage, key
 *   fetching, and crypto operations. In this file, keys are stored in NVM3.
 *   This file is only compiled for parts without Vault support.
 *******************************************************************************
 * # License
 * <b>Copyright 2022 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#include "stack/include/zigbee-security-manager.h"
#include "hal.h" // for TOKEN_resolution
#include "stack/include/security.h"
#include "stack/config/token-stack.h"
#include "stack/platform/micro/aes.h"
#include "stack/include/stack-info.h"
#include "stack/include/aes-mmo.h"
#include "stack/internal/inc/internal-defs-patch.h"
#ifdef SL_COMPONENT_CATALOG_PRESENT
#include "sl_component_catalog.h"
#endif
#include "stack/security/zigbee-security-manager-internal.h"

// Externs
extern void sli_zigbee_get_key_from_core(uint8_t* key);
extern void sli_util_load_key_into_core(const uint8_t* key);
extern sl_status_t sli_zigbee_add_transient_link_key(const sl_zigbee_key_struct_t* keyStruct);
extern sl_status_t sli_zigbee_get_transient_key_table_entry(uint8_t index, sl_zigbee_transient_key_data_t *transientKeyData);
extern bool sli_zigbee_get_trust_center_eui64(sl_802154_long_addr_t address);
extern bool findTransientLinkKey(const sl_802154_long_addr_t eui64ToFind,
                                 sl_zigbee_transient_key_data_t *keyDataReturn,
                                 sl_zigbee_key_struct_bitmask_t* bitmask);
extern bool removeTransientLinkKey(const sl_802154_long_addr_t eui64ToFind,
                                   sl_zigbee_key_struct_bitmask_t* bitmask);
extern void sli_zigbee_stack_token_primitive(bool tokenRead,
                                             void* tokenStruct,
                                             uint16_t tokenAddress,
                                             uint8_t length);
extern sl_status_t sli_zigbee_get_key_table_entry(uint8_t index, sl_zigbee_key_struct_t *result);
extern uint8_t sli_zigbee_find_key_table_entry(sl_802154_long_addr_t address, bool linkKey, uint8_t bitmask);
extern void sli_legacy_mfg_security_config_modify_key(sl_zigbee_key_data_t* key);
extern bool sli_zigbee_af_memory_byte_compare(const uint8_t *bytes, uint8_t count, uint8_t target);
extern bool sli_zigbee_is_token_data_initialized(uint8_t* data, uint8_t length);
extern sl_status_t sli_zigbee_af_set_key_table_entry(bool erase,
                                                     uint8_t index,
                                                     sl_zigbee_key_struct_t* keyStruct);
extern bool sli_zigbee_is_null_key(sl_zigbee_key_data_t * key);
extern uint32_t sli_zigbee_stack_get_security_frame_counter(void);
//used for software fallback of CCM*
extern void sli_util_stand_alone_encrypt_block(uint8_t *block);

// Globals
// The global context key, or key that is used in crypto operations
static uint8_t zb_sec_man_context_key[SL_ZIGBEE_ENCRYPTION_KEY_SIZE];

// A scratch key that we only need temporarily, like CBKE operations
static uint8_t zb_sec_man_internal_key[SL_ZIGBEE_ENCRYPTION_KEY_SIZE];

//Backup of the context key, in case anything called by zb_sec_man APIs
//needs to load an internal key.
static uint8_t zb_sec_man_context_key_backup[SL_ZIGBEE_ENCRYPTION_KEY_SIZE];

//MIC length to be used by AES-CCM.  Changing this to a value besides 4
//requires calling sli_zigbee_stack_sec_man_aes_ccm_extended directly (calling the
//regular sl_zigbee_sec_man_aes_ccm sets it back to 4).
static uint8_t stored_mic_length = 4;

// Eventually pull this from aps-keys-full.h
#define KEY_TABLE_ENTRY_HAS_PSA_ID          (BIT(6))

// This bit indicates if entry in the key table is a Symmetric Passphrase
#define KEY_TABLE_SYMMETRIC_PASSPHRASE      (BIT(7))

void sli_zigbee_stack_sec_man_init_context(sl_zigbee_sec_man_context_t* context)
{
  memset(context, 0, sizeof(*context));
}

//Stub (sets a value that is unused in this implementation)
void sli_zigbee_stack_sec_man_set_context_aes_ecb_alg(sl_zigbee_sec_man_context_t* context)
{
  (void) context;
}

void sli_zigbee_stack_sec_man_set_context_extended_ccm_alg(sl_zigbee_sec_man_context_t* context)
{
  (void) context;
}

sl_status_t zb_sec_man_store_nwk_key(sl_zigbee_sec_man_context_t* context,
                                     const sl_zigbee_sec_man_key_t* plaintext_key)
{
  tokTypeStackKeys tok;
  if (context->key_index == 1) {
    sli_zigbee_stack_token_primitive(true, &tok, TOKEN_STACK_ALTERNATE_KEY, TOKEN_STACK_ALTERNATE_KEY_SIZE);
    memmove(tok.networkKey, plaintext_key->key, sizeof(plaintext_key->key));
    sli_zigbee_stack_token_primitive(false, &tok, TOKEN_STACK_ALTERNATE_KEY, TOKEN_STACK_ALTERNATE_KEY_SIZE);
  } else {
    sli_zigbee_stack_token_primitive(true, &tok, TOKEN_STACK_KEYS, TOKEN_STACK_KEYS_SIZE);
    memmove(tok.networkKey, plaintext_key->key, sizeof(plaintext_key->key));
    sli_zigbee_stack_token_primitive(false, &tok, TOKEN_STACK_KEYS, TOKEN_STACK_KEYS_SIZE);
  }

  return SL_STATUS_OK;
}

sl_status_t zb_sec_man_fetch_nwk_key(sl_zigbee_sec_man_context_t* context,
                                     sl_zigbee_sec_man_key_t* plaintext_key)
{
  tokTypeStackKeys tok;
  if (context->key_index == 1) {
    sli_zigbee_stack_token_primitive(true, &tok, TOKEN_STACK_ALTERNATE_KEY, TOKEN_STACK_ALTERNATE_KEY_SIZE);
  } else {
    sli_zigbee_stack_token_primitive(true, &tok, TOKEN_STACK_KEYS, TOKEN_STACK_KEYS_SIZE);
  }
  memmove(plaintext_key->key, tok.networkKey, sizeof(plaintext_key->key));
  return SL_STATUS_OK;
}

sl_status_t zb_sec_man_store_tc_link_key(sl_zigbee_sec_man_context_t* context,
                                         const sl_zigbee_sec_man_key_t* plaintext_key)
{
  tokTypeStackTrustCenter tok;
  sli_zigbee_stack_token_primitive(true, &tok, TOKEN_STACK_TRUST_CENTER, TOKEN_STACK_TRUST_CENTER_SIZE);
  memmove(tok.key, plaintext_key->key, sizeof(plaintext_key->key));
  sli_zigbee_stack_token_primitive(false, &tok, TOKEN_STACK_TRUST_CENTER, TOKEN_STACK_TRUST_CENTER_SIZE);
  return SL_STATUS_OK;
}

sl_status_t zb_sec_man_fetch_tc_link_key(sl_zigbee_sec_man_context_t* context,
                                         sl_zigbee_sec_man_key_t* plaintext_key)
{
  sl_802154_long_addr_t tcAddress;
  if (sli_zigbee_stack_get_node_id() != 0x0000
      && sli_zigbee_get_trust_center_eui64(tcAddress)) {
    if ((context->flags & ZB_SEC_MAN_FLAG_EUI_IS_VALID)
        && (0 != memcmp(context->eui64, tcAddress, EUI64_SIZE))) {
      return SL_STATUS_NOT_FOUND;
    }
    memmove(context->eui64, tcAddress, EUI64_SIZE);
    context->flags |= ZB_SEC_MAN_FLAG_EUI_IS_VALID;
  }

  tokTypeStackTrustCenter tok;
  sli_zigbee_stack_token_primitive(true, &tok, TOKEN_STACK_TRUST_CENTER, TOKEN_STACK_TRUST_CENTER_SIZE);
  memmove(plaintext_key->key, tok.key, sizeof(plaintext_key->key));

  return SL_STATUS_OK;
}

sl_status_t zb_sec_man_store_transient_key(sl_zigbee_sec_man_context_t* context,
                                           const sl_zigbee_sec_man_key_t* plaintext_key)
{
  sl_status_t status;
  sl_zigbee_key_struct_t keyStruct;

  // Transient keys are typically used on Trust Centers
  // Advanced users can utilize them as another means of trying multiple keys
  // when joining a trust center (e.g. using a set of wildcard keys to act like
  // multiple preconfigured key slot keys)

  memset(&keyStruct, 0, sizeof(sl_zigbee_key_struct_t));
  memmove(keyStruct.partnerEUI64, context->eui64, EUI64_SIZE);
  memmove(sl_zigbee_key_contents(&(keyStruct.key)), plaintext_key, SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
  keyStruct.bitmask |= SL_ZIGBEE_KEY_HAS_KEY_DATA;
  keyStruct.type = SL_ZIGBEE_TRUST_CENTER_LINK_KEY;

  if (context->flags & ZB_SEC_MAN_FLAG_UNCONFIRMED_TRANSIENT_KEY) {
    keyStruct.bitmask |= SL_ZIGBEE_UNCONFIRMED_TRANSIENT_KEY;
  }
  if (context->flags & ZB_SEC_MAN_FLAG_AUTHENTICATED_DYNAMIC_LINK_KEY) {
    keyStruct.bitmask |= SL_ZIGBEE_DLK_DERIVED_KEY;
  }
  if (context->flags & ZB_SEC_MAN_FLAG_EUI_IS_VALID) {
    keyStruct.bitmask |= SL_ZIGBEE_KEY_HAS_PARTNER_EUI64;
  }

  status = sli_zigbee_add_transient_link_key(&keyStruct);

  return status;
}

sl_status_t zb_sec_man_fetch_transient_key(sl_zigbee_sec_man_context_t* context,
                                           sl_zigbee_sec_man_key_t* plaintext_key)
{
  sl_status_t status = SL_STATUS_FAIL;
  sl_zigbee_transient_key_data_t transientKeyData;

  if (context->flags & ZB_SEC_MAN_FLAG_KEY_INDEX_IS_VALID) {
    status = sli_zigbee_get_transient_key_table_entry(context->key_index, &transientKeyData);
  } else if (context->flags & ZB_SEC_MAN_FLAG_EUI_IS_VALID) {
    sl_zigbee_key_struct_bitmask_t bitmask = 0;
    if (context->flags & ZB_SEC_MAN_FLAG_UNCONFIRMED_TRANSIENT_KEY) {
      bitmask |= SL_ZIGBEE_UNCONFIRMED_TRANSIENT_KEY;
    }
    if (context->flags & ZB_SEC_MAN_FLAG_AUTHENTICATED_DYNAMIC_LINK_KEY) {
      bitmask |= SL_ZIGBEE_DLK_DERIVED_KEY;
    }
    bool found = findTransientLinkKey(context->eui64,
                                      &transientKeyData,
                                      bitmask != 0 ? &bitmask : NULL);
    status = found ? SL_STATUS_OK : SL_STATUS_NOT_FOUND;
  }

  if (status != SL_STATUS_OK) {
    return SL_STATUS_NOT_FOUND;
  }

  memmove(context->eui64, transientKeyData.eui64, EUI64_SIZE);
  context->multi_network_index = transientKeyData.networkIndex;
  if (transientKeyData.bitmask & SL_ZIGBEE_UNCONFIRMED_TRANSIENT_KEY) {
    context->flags |= ZB_SEC_MAN_FLAG_UNCONFIRMED_TRANSIENT_KEY;
  }
  if (transientKeyData.bitmask & SL_ZIGBEE_DLK_DERIVED_KEY) {
    context->flags |= ZB_SEC_MAN_FLAG_AUTHENTICATED_DYNAMIC_LINK_KEY;
  }
  if (transientKeyData.bitmask & SL_ZIGBEE_KEY_HAS_PARTNER_EUI64) {
    context->flags |= ZB_SEC_MAN_FLAG_EUI_IS_VALID;
  }
  memmove(plaintext_key->key, transientKeyData.keyData.contents, SL_ZIGBEE_ENCRYPTION_KEY_SIZE);

  return SL_STATUS_OK;
}

#if defined(SL_CATALOG_ZIGBEE_LIGHT_LINK_PRESENT) || defined(SL_ZIGBEE_TEST)
sl_status_t zb_sec_man_fetch_zll_key(sl_zigbee_sec_man_context_t* context,
                                     sl_zigbee_sec_man_key_t* plaintext_key)
{
  tokTypeStackZllSecurity zllSecurityToken;
  halCommonGetToken(&zllSecurityToken, TOKEN_STACK_ZLL_SECURITY);
  if (context->core_key_type == SL_ZB_SEC_MAN_KEY_TYPE_ZLL_ENCRYPTION_KEY) {
    memmove(plaintext_key->key,
            zllSecurityToken.encryptionKey,
            SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
  } else {
    memmove(plaintext_key->key,
            zllSecurityToken.preconfiguredKey,
            SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
  }
  return SL_STATUS_OK;
}

sl_status_t zb_sec_man_store_zll_key(sl_zigbee_sec_man_context_t* context,
                                     const sl_zigbee_sec_man_key_t* plaintext_key)
{
  tokTypeStackZllSecurity zllSecurityToken;
  halCommonGetToken(&zllSecurityToken, TOKEN_STACK_ZLL_SECURITY);
  if (context->core_key_type == SL_ZB_SEC_MAN_KEY_TYPE_ZLL_ENCRYPTION_KEY) {
    memmove(zllSecurityToken.encryptionKey,
            plaintext_key->key,
            SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
  } else {
    memmove(zllSecurityToken.preconfiguredKey,
            plaintext_key->key,
            SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
  }
  halCommonSetToken(TOKEN_STACK_ZLL_SECURITY, &zllSecurityToken);
  return SL_STATUS_OK;
}
#endif // defined(SL_CATALOG_ZIGBEE_LIGHT_LINK_PRESENT) || defined(SL_ZIGBEE_TEST)

#if defined(SL_CATALOG_ZIGBEE_GREEN_POWER_PRESENT) || defined(SL_ZIGBEE_TEST)
sl_status_t zb_sec_man_fetch_gp_key(sl_zigbee_sec_man_context_t* context,
                                    sl_zigbee_sec_man_key_t* plaintext_key)
{
  uint8_t index = context->key_index;
  if (context->core_key_type == SL_ZB_SEC_MAN_KEY_TYPE_GREEN_POWER_PROXY_TABLE_KEY) {
    tokTypeStackGpProxyTableEntry tok;
    halCommonGetIndexedToken(&tok, TOKEN_STACK_GP_PROXY_TABLE, index);
    memmove(plaintext_key->key, tok.gpdKey, SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
  } else {
    tokTypeStackGpSinkTableEntry tok;
    halCommonGetIndexedToken(&tok, TOKEN_STACK_GP_SINK_TABLE, index);
    memmove(plaintext_key->key, tok.gpdKey, SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
  }
  return SL_STATUS_OK;
}

sl_status_t zb_sec_man_store_gp_key(sl_zigbee_sec_man_context_t* context,
                                    const sl_zigbee_sec_man_key_t* plaintext_key)
{
  uint8_t index = context->key_index;
  if (context->core_key_type == SL_ZB_SEC_MAN_KEY_TYPE_GREEN_POWER_PROXY_TABLE_KEY) {
    tokTypeStackGpProxyTableEntry tok;
    halCommonGetIndexedToken(&tok, TOKEN_STACK_GP_PROXY_TABLE, index);
    memmove(tok.gpdKey, plaintext_key->key, SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
    halCommonSetIndexedToken(TOKEN_STACK_GP_PROXY_TABLE, index, &tok);
  } else {
    tokTypeStackGpSinkTableEntry tok;
    halCommonGetIndexedToken(&tok, TOKEN_STACK_GP_SINK_TABLE, index);
    memmove(tok.gpdKey, plaintext_key->key, SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
    halCommonSetIndexedToken(TOKEN_STACK_GP_SINK_TABLE, index, &tok);
  }
  return SL_STATUS_OK;
}
#endif // defined(SL_CATALOG_ZIGBEE_GREEN_POWER_PRESENT) || defined(SL_ZIGBEE_TEST)

sl_status_t zb_sec_man_store_in_link_key_table(sl_zigbee_sec_man_context_t* context,
                                               const sl_zigbee_sec_man_key_t* plaintext_key)
{
  sl_zigbee_key_struct_t keyStruct;
  memset(&keyStruct, 0, sizeof(sl_zigbee_key_struct_t));
  memmove(keyStruct.partnerEUI64, context->eui64, EUI64_SIZE);
  memmove(keyStruct.key.contents, plaintext_key->key, SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
  keyStruct.type = SL_ZIGBEE_APPLICATION_LINK_KEY;
  keyStruct.bitmask = context->flags & ZB_SEC_MAN_FLAG_SYMMETRIC_PASSPHRASE
                      ? SL_ZIGBEE_KEY_IS_AUTHENTICATION_TOKEN : 0;

  //Find a key index if 0xFF was passed in for it
  //(update existing key with this EUI or find an open entry)
  if (context->key_index == 0xFF) {
    sl_zigbee_sec_man_context_t temp_context;
    sli_zigbee_stack_sec_man_export_link_key_by_eui(context->eui64, &temp_context, NULL, NULL);
    if (temp_context.key_index == 0xFF) {
      sli_zigbee_stack_sec_man_export_link_key_by_eui(NULL, &temp_context, NULL, NULL);
      if (temp_context.key_index == 0xFF) {
        return SL_STATUS_FULL;
      }
    }
    context->key_index = temp_context.key_index;
  }

  sl_status_t status = sli_zigbee_af_set_key_table_entry(false,  // no delete
                                                         context->key_index,
                                                         &keyStruct);

  if (status == SL_STATUS_OK) {
    tokTypeStackKeyTable tok;
    halCommonGetIndexedToken(&tok, TOKEN_STACK_KEY_TABLE, context->key_index);
    memmove(&(tok[KEY_ENTRY_KEY_DATA_OFFSET]),
            plaintext_key->key,
            SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
    tok[KEY_ENTRY_INFO_OFFSET] |= SL_ZIGBEE_KEY_HAS_KEY_DATA;
    if (context->flags & ZB_SEC_MAN_FLAG_SYMMETRIC_PASSPHRASE) {
      tok[KEY_ENTRY_INFO_OFFSET] |= KEY_TABLE_SYMMETRIC_PASSPHRASE;
    }
    halCommonSetIndexedToken(TOKEN_STACK_KEY_TABLE, context->key_index, (void*)&tok);
  }

  return status;
}

sl_status_t zb_sec_man_fetch_from_link_key_table(sl_zigbee_sec_man_context_t* context,
                                                 sl_zigbee_sec_man_key_t* plaintext_key)
{
  sl_status_t status = SL_STATUS_FAIL;
  sl_zigbee_key_struct_t keyStruct;
  bool lookForEmptyEntry = sli_zigbee_af_memory_byte_compare(context->eui64, EUI64_SIZE, 0x0);

  if (context->flags & ZB_SEC_MAN_FLAG_KEY_INDEX_IS_VALID) {
    status = sli_zigbee_get_key_table_entry(context->key_index, &keyStruct);
    memmove(context->eui64, keyStruct.partnerEUI64, EUI64_SIZE);
  } else if (context->flags & ZB_SEC_MAN_FLAG_EUI_IS_VALID) {
    uint8_t bitmask = (context->flags & ZB_SEC_MAN_FLAG_SYMMETRIC_PASSPHRASE) ? KEY_TABLE_SYMMETRIC_PASSPHRASE : 0;
    context->key_index = sli_zigbee_find_key_table_entry(context->eui64, true, bitmask); // linkKey
    status = (context->key_index != 0xFF) ? SL_STATUS_OK : SL_STATUS_NOT_FOUND;
    if (context->key_index != 0xFF && !lookForEmptyEntry) {
      status = sli_zigbee_get_key_table_entry(context->key_index, &keyStruct);
    }
  }

  if (status != SL_STATUS_OK) {
    return SL_STATUS_NOT_FOUND;
  }

  tokTypeStackKeyTable tok;
  halCommonGetIndexedToken(&tok, TOKEN_STACK_KEY_TABLE, context->key_index);
  // Ensure that the token does not point to a PSA ID.  If this asserts, it
  // means that you have attempted to downgrade from an image that stored keys in
  // secure vault to an image that does not; this is prohibited.  The token
  // houses the ID in PSA where the key lives, and since we don't have PSA key
  // storage support enabled, we can't possibly function correctly.
  // This is a misconfiguration.  Either flash an image with the Zigbee Secure Key Storage
  // component or masserase to blow away your tokens (which will wipe your persistence)
  assert((tok[KEY_ENTRY_INFO_OFFSET] & KEY_TABLE_ENTRY_HAS_PSA_ID) == 0);
  memmove(plaintext_key->key,
          &(tok[KEY_ENTRY_KEY_DATA_OFFSET]),
          SL_ZIGBEE_ENCRYPTION_KEY_SIZE);

  return SL_STATUS_OK;
}

sl_status_t sli_zigbee_stack_sec_man_delete_transient_key(sl_zigbee_sec_man_context_t* context)
{
  sl_zigbee_transient_key_data_t transientKeyData = { 0 };

  if (context->flags & ZB_SEC_MAN_FLAG_UNCONFIRMED_TRANSIENT_KEY) {
    transientKeyData.bitmask |= SL_ZIGBEE_UNCONFIRMED_TRANSIENT_KEY;
  }
  if (context->flags & ZB_SEC_MAN_FLAG_AUTHENTICATED_DYNAMIC_LINK_KEY) {
    transientKeyData.bitmask |= SL_ZIGBEE_DLK_DERIVED_KEY;
  }

  if (context->flags & ZB_SEC_MAN_FLAG_KEY_INDEX_IS_VALID) {
    sl_status_t status = sli_zigbee_get_transient_key_table_entry(context->key_index, &transientKeyData);
    if (status != SL_STATUS_OK) {
      return SL_STATUS_NOT_FOUND;
    }
    //Allows deleting transient keys by index (for clearing the table)
    memmove(context->eui64, transientKeyData.eui64, sizeof(sl_802154_long_addr_t));
    context->flags |= ZB_SEC_MAN_FLAG_EUI_IS_VALID;
  }

  return (removeTransientLinkKey(context->eui64, &transientKeyData.bitmask) ? SL_STATUS_OK : SL_STATUS_NOT_FOUND);
}

sl_status_t sli_zigbee_stack_sec_man_delete_key_table_key(sl_zigbee_sec_man_context_t* context)
{
  sl_status_t status = SL_STATUS_FAIL;

  if (context->flags & ZB_SEC_MAN_FLAG_EUI_IS_VALID) {
    uint8_t bitmask = (context->flags & ZB_SEC_MAN_FLAG_SYMMETRIC_PASSPHRASE) ? KEY_TABLE_SYMMETRIC_PASSPHRASE : 0;
    context->key_index = sli_zigbee_find_key_table_entry(context->eui64, true, bitmask); // linkKey
  }

  if (context->key_index != 0xFF) {
    sl_zigbee_key_struct_t keyStruct;
    memset(&keyStruct, 0, sizeof(sl_zigbee_key_struct_t));
    status = sli_zigbee_af_set_key_table_entry(true,   // delete
                                               context->key_index,
                                               NULL); // key data, don't care
  }

  return status;
}

//helper function for HMAC w/ AES-MMO, ported from hmac.c to avoid
//dependency on the deprecated sl_zigbee_hmac_aes_hash API
static void xorKeyWithByte(const uint8_t key[ENCRYPTION_BLOCK_SIZE],
                           uint8_t byte,
                           uint8_t result[ENCRYPTION_BLOCK_SIZE])
{
  uint8_t i;
  for (i = 0; i < ENCRYPTION_BLOCK_SIZE; i++) {
    result[i] = key[i] ^ byte;
  }
}

//Code ported over from sl_zigbee_hmac_aes_hash; has Zigbee security manager
//handle key loading instead of taking in key data as an argument.

void sli_zigbee_stack_sec_man_hmac_aes_mmo(const uint8_t* input,
                                           const uint8_t data_length,
                                           uint8_t* output)
{
  //load key from other function calls first
  sli_zigbee_get_key_from_core((uint8_t*) &zb_sec_man_context_key);

  uint8_t buffer[ENCRYPTION_BLOCK_SIZE];
  uint8_t run;
  sl_zigbee_aes_mmo_hash_context_t context;

  for (run = 0; run < 2; run++) {
    xorKeyWithByte((const uint8_t*) &zb_sec_man_context_key,
                   (run == 0
                    ? 0x36
                    : 0x5C),
                   buffer);

    sli_zigbee_stack_aes_mmo_hash_init(&context);
    sli_zigbee_stack_aes_mmo_hash_update(&context, ENCRYPTION_BLOCK_SIZE, buffer);
    sli_zigbee_stack_aes_mmo_hash_final(&context,
                                        (run == 0
                                         ? data_length
                                         : ENCRYPTION_BLOCK_SIZE),
                                        (run == 0
                                         ? input
                                         : output));
    memmove(output, context.result, ENCRYPTION_BLOCK_SIZE);
  }
}

#if defined(MBEDTLS_PSA_ACCEL_KEY_TYPE_AES) && defined(MBEDTLS_PSA_ACCEL_ALG_CCM) && defined(MBEDTLS_PSA_CRYPTO_DRIVERS)
sl_status_t sli_zigbee_stack_sec_man_aes_ccm_extended(uint8_t* nonce,
                                                      bool encrypt,
                                                      const uint8_t* input,
                                                      uint8_t encryption_start_index,
                                                      uint8_t length,
                                                      uint8_t mic_length,
                                                      uint8_t* output)
{
  stored_mic_length = mic_length;
  //Ported over from the PSA implementations inside ccm-star.c
  psa_status_t psa_status;
  size_t tag_len;
  size_t output_length;
  psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;

  psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
  psa_set_key_bits(&attr, PSA_BYTES_TO_BITS(sizeof(zb_sec_man_context_key)));

  //copy the auth data to output buffer
  if (input != output) {
    memmove(output, input, encryption_start_index);
  }

  if (encrypt) {
    psa_status = AEAD_ENCRYPT_TAG_FCT(
      &attr, zb_sec_man_context_key, sizeof(zb_sec_man_context_key),
      PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, stored_mic_length),
      nonce, NONCE_LENGTH,
      input, encryption_start_index,
      input + encryption_start_index, length - encryption_start_index,
      output + encryption_start_index, length - encryption_start_index, &output_length,
      output + length, stored_mic_length, &tag_len);
  } else {
    psa_status = AEAD_DECRYPT_TAG_FCT(
      &attr, zb_sec_man_context_key, sizeof(zb_sec_man_context_key),
      PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, stored_mic_length),
      nonce, NONCE_LENGTH,
      input, encryption_start_index,
      input + encryption_start_index, length - encryption_start_index,
      input + length, stored_mic_length,
      output + encryption_start_index, length - encryption_start_index, &output_length);
  }

  psa_reset_key_attributes(&attr);

  if (psa_status == PSA_ERROR_INVALID_SIGNATURE) {
    return SL_STATUS_INVALID_SIGNATURE;
  }

  if (psa_status != PSA_SUCCESS || output_length != (uint8_t) (length - encryption_start_index)
      || (encrypt && tag_len != stored_mic_length)) {
    return SL_STATUS_INVALID_PARAMETER;
  } else {
    return SL_STATUS_OK;
  }
}

sl_status_t sli_zigbee_stack_sec_man_aes_128_crypt_block(bool encrypt,
                                                         const uint8_t* input,
                                                         uint8_t* output)
{
  //Ported from PSA wrapper versions of sli_zigbee_aes_encrypt/Decrypt
  size_t output_size = 0;
  psa_key_attributes_t key_attr = PSA_KEY_ATTRIBUTES_INIT;

  psa_set_key_type(&key_attr, PSA_KEY_TYPE_AES);
  psa_set_key_bits(&key_attr, PSA_BYTES_TO_BITS(SL_ZIGBEE_ENCRYPTION_KEY_SIZE));
  psa_status_t psa_status;

  if (encrypt) {
    psa_status = CIPHER_SINGLE_SHOT_ENC_FCT(
      &key_attr,
      (const uint8_t*) &zb_sec_man_context_key,
      SL_ZIGBEE_ENCRYPTION_KEY_SIZE,
      PSA_ALG_ECB_NO_PADDING,
      NULL, 0,
      input, SECURITY_BLOCK_SIZE,
      output, SECURITY_BLOCK_SIZE,
      &output_size);
  } else {
    psa_status = CIPHER_SINGLE_SHOT_DEC_FCT(
      &key_attr,
      (const uint8_t*) &zb_sec_man_context_key,
      SL_ZIGBEE_ENCRYPTION_KEY_SIZE,
      PSA_ALG_ECB_NO_PADDING,
      input, SECURITY_BLOCK_SIZE,
      output, SECURITY_BLOCK_SIZE,
      &output_size);
  }

  psa_reset_key_attributes(&key_attr);
  assert((psa_status == PSA_SUCCESS)
         && (output_size == ENCRYPTION_BLOCK_SIZE));
  return SL_STATUS_OK;
}

#else //no support for PSA APIs at all; compile with software implementation of CCM*.

#define STANDALONE_FLAGS_INDEX                   0
#define STANDALONE_NONCE_INDEX                   1
#define STANDALONE_VARIABLE_FIELD_INDEX_HIGH    14
#define STANDALONE_VARIABLE_FIELD_INDEX_LOW     15

#ifndef TEMP_BUFFER_SIZE
#define TEMP_BUFFER_SIZE 256
#endif

static uint8_t encryptionBlock[ENCRYPTION_BLOCK_SIZE];

static void encryptNonce(uint8_t *nonce,
                         uint8_t flagsMask,
                         uint16_t variableField,
                         uint8_t block[ENCRYPTION_BLOCK_SIZE])
{
  memcpy(block + STANDALONE_NONCE_INDEX, nonce, NONCE_LENGTH);

  block[STANDALONE_FLAGS_INDEX] = flagsMask;
  block[STANDALONE_VARIABLE_FIELD_INDEX_HIGH] = HIGH_BYTE(variableField);
  block[STANDALONE_VARIABLE_FIELD_INDEX_LOW] = LOW_BYTE(variableField);

  sli_util_stand_alone_encrypt_block(block);
}

static uint16_t min(uint16_t num1,
                    uint16_t num2)
{
  if (num1 <= num2) {
    return num1;
  }
  return num2;
}

static uint8_t xorBytesIntoBlock(uint8_t *block,
                                 uint8_t blockIndex,
                                 const uint8_t *bytes,
                                 uint16_t count)
{
  uint16_t i;

  for (i = 0; i < count; ) {
    uint16_t needed = ENCRYPTION_BLOCK_SIZE - blockIndex;
    uint16_t todo = count - i;
    uint16_t copied = min(todo, needed);
    uint16_t j;

    for (j = 0; j < copied; j++, blockIndex++) {
      block[blockIndex] ^= *bytes++;
    }
    i += copied;

    if (blockIndex == ENCRYPTION_BLOCK_SIZE) {
      sli_util_stand_alone_encrypt_block(block);
      blockIndex = 0;
    }
  }
  return blockIndex;
}

static void ccmEncryptData(uint8_t *bytes,
                           uint16_t length,
                           uint8_t blockCount,
                           uint8_t *nonce)
{
  while (length > 0) {
    uint8_t todo = min(length, ENCRYPTION_BLOCK_SIZE);
    uint8_t i;
    encryptNonce(nonce, 0x01, blockCount, encryptionBlock);
    blockCount += 1;

    for (i = 0; i < todo; i++) {
      *bytes++ ^= encryptionBlock[i];
    }

    length -= todo;
  }
}

static sl_status_t zb_sec_man_calc_encrypt_mic(uint8_t* nonce,
                                               uint8_t* data,
                                               uint8_t encryption_start_index,
                                               uint8_t length)
{
  uint8_t blockIndex = 2;     // skip over frame length field
  uint8_t *chunk = data;
  uint16_t chunklen;
  uint8_t phase;

  chunklen = encryption_start_index;
  encryptNonce(nonce, 0x49, length - encryption_start_index, encryptionBlock);
  // first two bytes are the 16-bit representation of the frame length,
  // high byte first
  encryptionBlock[0] ^= HIGH_BYTE(encryption_start_index);
  encryptionBlock[1] ^= LOW_BYTE(encryption_start_index);
  // phase 0: go over authenticated data
  // phase 1: go over the payload
  for (phase = 0; phase < 2; phase++) {
    blockIndex = xorBytesIntoBlock(encryptionBlock,
                                   blockIndex,
                                   chunk,
                                   chunklen);
    chunk = data + encryption_start_index;
    chunklen = length - encryption_start_index;

    // finish off authData if not on an encryption block boundary
    if (blockIndex > 0) {
      sli_util_stand_alone_encrypt_block(encryptionBlock);
      blockIndex = 0;
    }
  }

  memcpy(data + length, encryptionBlock, stored_mic_length);
  // The MIC gets encrypted as block zero of the message.
  ccmEncryptData(data + length, stored_mic_length, 0, nonce);
  return SL_STATUS_OK;
}

//Software implementation here is based on implementation for emberCcmCalculateAndEncryptMic,
//but assumes packet input/output format.
//output points to auth data, with encrypt data at encryption_start_index past it and MIC
//starting at output + length (mic_length bytes long)
sl_status_t sli_zigbee_stack_sec_man_aes_ccm_extended(uint8_t* nonce,
                                                      bool encrypt,
                                                      const uint8_t* input,
                                                      uint8_t encryption_start_index,
                                                      uint8_t length,
                                                      uint8_t mic_length,
                                                      uint8_t* output)
{
  stored_mic_length = mic_length;
  //Move input into output location for in-place encryption
  memcpy(output, input, length);
  bool authenticated;
  if (encrypt) {
    zb_sec_man_calc_encrypt_mic(nonce, output, encryption_start_index, length);
    if (length > encryption_start_index) {
      ccmEncryptData(output + encryption_start_index, length - encryption_start_index, 1, nonce);
    }
  } else {
    ccmEncryptData(output + encryption_start_index, length - encryption_start_index, 1, nonce);
    uint8_t temp[TEMP_BUFFER_SIZE];
    memcpy(temp, output, length);
    zb_sec_man_calc_encrypt_mic(nonce, temp, encryption_start_index, length);
    authenticated = memcmp(input + length, temp + length, stored_mic_length) == 0;
    if (!authenticated) {
      //MIC check failed
      return SL_STATUS_INVALID_SIGNATURE;
    }
  }

  return SL_STATUS_OK;
}

sl_status_t sli_zigbee_stack_sec_man_aes_128_crypt_block(bool encrypt,
                                                         const uint8_t* input,
                                                         uint8_t* output)
{
  memcpy(output, input, ENCRYPTION_BLOCK_SIZE);
  // On hardware, this calls PSA, else our software implementation in aes-ecb.c
  // is called (on zigbeed, for instance)
  if (encrypt) {
    sli_zigbee_aes_encrypt(output, (const uint8_t*)&zb_sec_man_context_key);
  } else {
    sli_zigbee_aes_decrypt(output, (const uint8_t*)&zb_sec_man_context_key);
  }
  return SL_STATUS_OK;
}

#endif // defined(MBEDTLS_PSA_ACCEL_KEY_TYPE_AES) && defined(MBEDTLS_PSA_ACCEL_ALG_CCM) && defined(PSA_WANT_ALG_CCM) && defined(MBEDTLS_PSA_CRYPTO_DRIVERS)

sl_status_t sli_zigbee_stack_sec_man_check_key_context(sl_zigbee_sec_man_context_t* context)
{
  //finds the key itself for non-Vault implementation, but doesn't return it
  sl_zigbee_sec_man_key_t test_key_data;

  //If export key succeeds, the context is able to properly extract a key.
  //Returns not found if context couldn't be used to load a key.
  sl_status_t export_status = sli_zigbee_stack_sec_man_export_key(context, &test_key_data);
  if (export_status == SL_STATUS_OK) {
    return SL_STATUS_OK;
  }
  return SL_STATUS_NOT_FOUND;
}

//generate derived key from security context, key, and requested type.
sl_status_t zb_sec_man_derive_key(sl_zigbee_sec_man_key_t* source_key,
                                  const sl_zigbee_sec_man_context_t* context,
                                  sl_zigbee_sec_man_key_t* derived_key)
{
  uint8_t tag;

  //source_key is used as the key for hashing;
  //When loading a key context that includes a derived key,
  //callers must set the core key type correctly to ensure that
  //key derivation is done correctly.

  sli_util_load_key_into_core((uint8_t*) source_key);
  bool has_first_derivation = false;

  if ((context->derived_type & SL_ZB_SEC_MAN_DERIVED_KEY_TYPE_TC_SWAP_OUT_KEY) != 0) {
    sli_zigbee_stack_aes_hash_simple(SL_ZIGBEE_ENCRYPTION_KEY_SIZE,
                                     (const uint8_t*) source_key,
                                     (uint8_t* ) derived_key);
    has_first_derivation = true;
  } else if ((context->derived_type & SL_ZB_SEC_MAN_DERIVED_KEY_TYPE_TC_HASHED_LINK_KEY) != 0) {
    sli_zigbee_stack_sec_man_hmac_aes_mmo(context->eui64, EUI64_SIZE, (uint8_t*) derived_key);
    has_first_derivation = true;
  }
  if (has_first_derivation) {
    //If key has multiple valid derived types, it will be one from the previous cases
    //and one from the following (as some derived types are treated more like ordinary link keys).
    //Load the first derivation in at this point to have the second derivation operation computed from it.
    sli_util_load_key_into_core((uint8_t*) derived_key);
  }
  if ((context->derived_type & SL_ZB_SEC_MAN_DERIVED_KEY_TYPE_KEY_TRANSPORT_KEY) != 0) {
    tag = DERIVE_TRANSPORT_KEY_TAG;
    sli_zigbee_stack_sec_man_hmac_aes_mmo(&tag, 1, (uint8_t*)derived_key);
  } else if ((context->derived_type & SL_ZB_SEC_MAN_DERIVED_KEY_TYPE_KEY_LOAD_KEY) != 0) {
    tag = DERIVE_LOAD_KEY_TAG;
    sli_zigbee_stack_sec_man_hmac_aes_mmo(&tag, 1, (uint8_t*) derived_key);
  } else if ((context->derived_type & SL_ZB_SEC_MAN_DERIVED_KEY_TYPE_VERIFY_KEY) != 0) {
    tag = DERIVE_VERIFY_KEY_TAG;
    sli_zigbee_stack_sec_man_hmac_aes_mmo(&tag, 1, (uint8_t*) derived_key);
  }
  if (context->derived_type == SL_ZB_SEC_MAN_DERIVED_KEY_TYPE_NONE) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  return SL_STATUS_OK;
}

sl_status_t sli_zigbee_stack_sec_man_delete_key(sl_zigbee_sec_man_context_t* context)
{
  sl_status_t status = SL_STATUS_INVALID_PARAMETER;

  // Every non-indexed key is simply overwritten with 0s
  sl_zigbee_sec_man_key_t null_key;
  memset(null_key.key, 0, sizeof(null_key.key));

  switch (context->core_key_type) {
    case SL_ZB_SEC_MAN_KEY_TYPE_NETWORK:
    case SL_ZB_SEC_MAN_KEY_TYPE_TC_LINK:
#if defined(SL_CATALOG_ZIGBEE_LIGHT_LINK_PRESENT) || defined(SL_ZIGBEE_TEST)
    case SL_ZB_SEC_MAN_KEY_TYPE_ZLL_ENCRYPTION_KEY:
    case SL_ZB_SEC_MAN_KEY_TYPE_ZLL_PRECONFIGURED_KEY:
#endif // defined(SL_CATALOG_ZIGBEE_LIGHT_LINK_PRESENT) || defined(SL_ZIGBEE_TEST)
#if defined(SL_CATALOG_ZIGBEE_GREEN_POWER_PRESENT) || defined(SL_ZIGBEE_TEST)
    case SL_ZB_SEC_MAN_KEY_TYPE_GREEN_POWER_PROXY_TABLE_KEY:
    case SL_ZB_SEC_MAN_KEY_TYPE_GREEN_POWER_SINK_TABLE_KEY:
#endif // defined(SL_CATALOG_ZIGBEE_GREEN_POWER_PRESENT) || defined(SL_ZIGBEE_TEST)
    case SL_ZB_SEC_MAN_KEY_TYPE_INTERNAL:
      status = sli_zigbee_stack_sec_man_import_key(context, &null_key);
      break;
    // Everything starting here is an indexed key
    case SL_ZB_SEC_MAN_KEY_TYPE_TC_LINK_WITH_TIMEOUT:
      status = sli_zigbee_stack_sec_man_delete_transient_key(context);
      break;
    case SL_ZB_SEC_MAN_KEY_TYPE_APP_LINK:
      status = sli_zigbee_stack_sec_man_delete_key_table_key(context);
      break;
    default:
      break;
  }

  return status;
}

/*
 * This function will load the specified core key type into the Security Manager's internal Context.
 * If the derived key type is one other than SL_ZB_SEC_MAN_DERIVED_KEY_TYPE_NONE it will derive the requested key
 * and load it into the Zigbee Security Manager context.
 * Any subsequent encryption or hash operations will be done with that loaded context.
 * For SL_ZB_SEC_MAN_KEY_TYPE_NETWORK, the key index is the primary/alternate key identifier.
 * For SL_ZB_SEC_MAN_KEY_TYPE_TC_LINK:
 * If the device is the trust center the key index will be the link key table entry.
 * If the device is a regular node it will load the device's trust center link key.
 *
 */
sl_status_t sli_zigbee_stack_sec_man_load_key_context(sl_zigbee_sec_man_context_t* context)
{
  sl_zigbee_sec_man_key_t plaintext_key;
  sli_zigbee_stack_sec_man_export_key(context, &plaintext_key);
  if (context->derived_type != SL_ZB_SEC_MAN_DERIVED_KEY_TYPE_NONE) {
    sl_zigbee_sec_man_key_t derived_key;
    zb_sec_man_derive_key(&plaintext_key, context, &derived_key);
    memcpy(&zb_sec_man_context_key, &derived_key, SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
  } else {
    memcpy(&zb_sec_man_context_key, &plaintext_key, SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
  }
  //TODO: Investigate removing this (and only using zb_sec_man_context_key as the "core")
  sli_util_load_key_into_core((const uint8_t *)&zb_sec_man_context_key);
  return SL_STATUS_OK;
}

//allows some AES-MMO functions to use sl_zb_sec_man APIs for AES encryption
void zb_sec_man_backup_key_context(bool direction)
{
  if (direction) {
    memmove(&zb_sec_man_context_key_backup, &zb_sec_man_context_key, SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
  } else {
    memmove(&zb_sec_man_context_key, &zb_sec_man_context_key_backup, SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
  }
}

sl_status_t zb_sec_man_delete_key_by_psa_id(UNUSED uint32_t psa_id)
{
  return SL_STATUS_NOT_SUPPORTED;
}

sl_status_t zb_sec_man_store_internal_key(sl_zigbee_sec_man_context_t* context,
                                          const sl_zigbee_sec_man_key_t* plaintext_key)
{
  memmove(&zb_sec_man_internal_key, plaintext_key, SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
  return SL_STATUS_OK;
}

sl_status_t zb_sec_man_fetch_internal_key(sl_zigbee_sec_man_context_t* context,
                                          sl_zigbee_sec_man_key_t* plaintext_key)
{
  memmove(plaintext_key, &zb_sec_man_internal_key, SL_ZIGBEE_ENCRYPTION_KEY_SIZE);
  return SL_STATUS_OK;
}

sl_status_t sli_zigbee_stack_sec_man_get_network_key_info(sl_zigbee_sec_man_network_key_info_t * network_key_info)
{
  tokTypeStackKeys tok;
  //Fetch Alternate nwk key info
  sli_zigbee_stack_token_primitive(true, &tok, TOKEN_STACK_ALTERNATE_KEY, TOKEN_STACK_ALTERNATE_KEY_SIZE);
  network_key_info->alt_network_key_sequence_number = tok.activeKeySeqNum;
  network_key_info->alternate_network_key_set = !sli_zigbee_is_null_key((sl_zigbee_key_data_t*)tok.networkKey);
  //Fetch nwk key info
  sli_zigbee_stack_token_primitive(true, &tok, TOKEN_STACK_KEYS, TOKEN_STACK_KEYS_SIZE);
  network_key_info->network_key_sequence_number = tok.activeKeySeqNum;
  network_key_info->network_key_set = !sli_zigbee_is_null_key((sl_zigbee_key_data_t*)tok.networkKey);
  // Fetch nwk key frame counter info
  network_key_info->network_key_frame_counter = sli_zigbee_stack_get_security_frame_counter();
  return SL_STATUS_OK;
}

bool sli_zigbee_stack_sec_man_compare_key_to_value(sl_zigbee_sec_man_context_t* context, const sl_zigbee_sec_man_key_t* test_key)
{
  sl_zigbee_sec_man_key_t plaintext_key;
  sli_zigbee_stack_sec_man_export_key(context, &plaintext_key);

  if (memcmp(&plaintext_key, test_key, SL_ZIGBEE_ENCRYPTION_KEY_SIZE) == 0) {
    return true;
  }

  return false;
}
