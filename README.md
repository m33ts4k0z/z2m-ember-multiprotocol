## Silicon Laboratories multiprotocol container with Zigbee2Mqtt bundled. Based on Simplicity SDK 2024.6.1

This is an **experimental** multiprotocol container supporting **Zigbee EmberZNet**, **OpenThread** and **Bluetooth Low Energy** using the same dongle. 

This container is **not** using the official [silabs multiprotocol container](https://hub.docker.com/r/siliconlabsinc/multiprotocol) but instead, it uses the [precompiled binaries by Nerivec](https://github.com/Nerivec/silabs-multiprotocol-builder/releases/).

Silicon labs doesn't offer an amd64 container (Thanks Nerivec for the incredible work of compiling everything from source)

> [!NOTE]
> You will need the dongle firmware compiled against the Simplicity SDK 8.0 for this container to work with your dongle.
> Grab it from [here](https://github.com/Nerivec/silabs-firmware-builder/releases/tag/v2024.6.1) (the blehci ones) and use [ember-zli](https://github.com/Nerivec/ember-zli) to flash it to your dongle.
> After flashing this firmware, you will enable even Bluetooth LE on your dongle something that isn't available in any other firmware.

> [!WARNING]
> After flashing the firmware above, ezsp will no longer be an option in Zigbee2Mqtt. Use ember instead. Of course you can always flash a 7.4 firmware again in order to use ezsp.

> [!CAUTION]
> Only the Sonoff Dongle-E and NabuCasa Skyconnect are tested with these firmwares. Use at your own risk
### 2 Ways to deploy

You can either use the run command:

```
docker run
--privileged
--net host
-v .env:/.env
-v your_zigbee2mqtt_data_directory:/opt/zigbee2mqtt/data
-p 8080:8080
-p 81:8785
-d
--device=/dev/ttyACM0:/dev/ttyACM0
-it m33ts4k0z/z2m-ember-multiprotocol:latest
```

Or using the docker-compose file provided.

Port 8080 is for the Zigbee2Mqtt application and 8785 is for the OpenThread Border Router (OTBR) Web Interface.

Use the .env file to edit your configuration and restart the container in order for the changes to take effect.

Don't forget to change paths and/or devices where necessary.

Remember this is still under testing but feel free to report issues 😊


References:

https://www.silabs.com/documents/public/application-notes/an1333-concurrent-protocols-with-802-15-4-rcp.pdf

