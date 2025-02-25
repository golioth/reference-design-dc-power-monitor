# Golioth DC Power Monitor Reference Design

The Golioth DC Power Monitor is an IoT reference design that includes
two channels for monitoring equipment that is powered by Direct Current
(DC). This is commonly needed for battery management systems like
electric cars or bikes. Having reliable data on the state of the
charge/discharge cycles over a period of time makes it possible to
perform predictive maintenance and alert when levels are running low.

This reference design uses two ina260 current/voltage/power measurement
chips to measure the circuits passing through them. Readings from each
channel are passed up to Golioth via a Nordic nRF9160 cellular modem for
tracking usage over time. Live "run" time is also reported to show how
long a device has currently been running. This data is also used to
report the lifetime "run" time of the equipment being monitored. The
delay between readings and the threshold at which the equipment is
considered "off" are configurable from the Golioth cloud.

Business use cases and hardware build details are available on [the
DC Power Monitor Project
Page](https://projects.golioth.io/reference-designs/dc-power-monitor/).

This repository contains the firmware source code and [pre-built release
firmware
images](https://github.com/golioth/reference-design-dc-power-monitor/releases).

## Supported Hardware

- Nordic nRF9160-DK
- Golioth Aludel Elixir
- Golioth Aludel Mini

### Additional Sensors/Components

- Texas Instruments INA260 current/voltage/power monitor (x2)

## Golioth Features

This app implements:

  - [Device Settings
    Service](https://docs.golioth.io/firmware/golioth-firmware-sdk/device-settings-service)
  - [Remote Procedure Call
    (RPC)](https://docs.golioth.io/firmware/golioth-firmware-sdk/remote-procedure-call)
  - [Stream
    Client](https://docs.golioth.io/firmware/golioth-firmware-sdk/stream-client)
  - [LightDB State
    Client](https://docs.golioth.io/firmware/golioth-firmware-sdk/light-db-state/)
  - [Over-the-Air (OTA) Firmware
    Upgrade](https://docs.golioth.io/firmware/golioth-firmware-sdk/firmware-upgrade/firmware-upgrade)
  - [Backend
    Logging](https://docs.golioth.io/device-management/logging/)

### Settings Service

The following settings should be set in the Device Settings menu of the
[Golioth Console](https://console.golioth.io).

  - `LOOP_DELAY_S`
    Adjusts the delay between sensor readings. Set to an integer value
    (seconds).

    Default value is `60` seconds.

  - `ADC_FLOOR_CH0` (raw ADC value)
  - `ADC_FLOOR_CH1` (raw ADC value)
    Filter out noise by adjusting the minimum reading at which a channel
    will be considered "on".

    Default values are `0`

### Remote Procedure Call (RPC) Service

The following RPCs can be initiated in the Remote Procedure Call menu of
the [Golioth Console](https://console.golioth.io).

  - `get_network_info`
    Query and return network information.

  - `reboot`
    Reboot the system.

  - `set_log_level`
    Set the log level.

    The method takes a single parameter which can be one of the
    following integer values:

      - `0`: `LOG_LEVEL_NONE`
      - `1`: `LOG_LEVEL_ERR`
      - `2`: `LOG_LEVEL_WRN`
      - `3`: `LOG_LEVEL_INF`
      - `4`: `LOG_LEVEL_DBG`

### LightDB State and LightDB Stream data

#### Time-Series Data (LightDB Stream)

Current, Voltage, and Power data for both channels are reported as
time-series data on the `sensor` path. These readings can each be
multiplied by 0.00125 to convert the values to Amps, Volts, and Watts.

  - `sensor/cur/ch0`: Current for channel 0
  - `sensor/cur/ch0`: Current for channel 1
  - `sensor/pow/ch0`: Power for channel 0
  - `sensor/pow/ch0`: Power for channel 1
  - `sensor/vol/ch0`: Voltage for channel 0
  - `sensor/vol/ch0`: Voltage for channel 1

``` json
{
  "sensor": {
    "cur": {
       "ch0": 1,
       "ch1": 292
    },
    "pow": {
      "ch0": 0,
      "ch1": 187
    },
    "vol": {
      "ch0": 4106,
      "ch1": 4110
    }
  }
}
```

If your board includes a battery, voltage and level readings will be
sent to the `battery` endpoint.

> [!NOTE]
> Your Golioth project must have a Pipeline enabled to receive this
> data. See the [Add Pipeline to Golioth](#add-pipeline-to-golioth)
> section below.

#### Stateful Data (LightDB State)

The concept of Digital Twin is demonstrated with the LightDB State via
the `desired` and `actual` endpoints.

The concept of Digital Twin is demonstrated with the LightDB State via
the `state` path. Values will be updated by the device. The cloud may
read the `state` path to determine device status, but only the device
should ever write to that path.

  - `desired/reset_cumulative` values may be changed from the cloud
    side. The device will recognize when this path is set to `true`,
    clearing the stored `cumulative` values and writing the
    `reset_cumulative` value to `false` to indicate the operation was
    completed.
  - `state/cumulative` values indicate the sum of all time a current is
    detected on a channel throughout all on/off cycles.
  - `state/live_runtime` values reflect the time a current has been
    continuously detected on the channel since the state of the
    equipment being monitored changed from "off" to "on".

``` json
{
  "desired": {
    "reset_cumulative": false
  },
  "state": {
    "cumulative": {
      "ch0": 138141,
      "ch1": 1913952
    },
    "live_runtime": {
      "ch0": 0,
      "ch1": 913826
    }
  }
}
```

### OTA Firmware Update

This application includes the ability to perform Over-the-Air (OTA)
firmware updates. To do so, you need a binary compiled with a different
version number than what is currently running on the device.

> [!NOTE]
> If a newer release is available than what your device is currently
> running, you may download the pre-compiled binary that ends in
> `_update.bin` and use it in step 2 below.

1. Update the version number in the `VERSION` file and perform a
   pristine (important) build to incorporate the version change.
2. Upload the `build/app/zephyr/zephyr.signed.bin` file as a Package for
   your Golioth project.

   - Use `main` as the package name.
   - Use the same version number from step 1.

3. Create a Cohort and add your device to it.
4. Create a Deployment for your Cohort using the package name and
   version number from step 2.
5. Devices in your Cohort will automatically upgrade to the most
   recently deployed firmware.

Visit [the Golioth Docs OTA Firmware Upgrade
page](https://docs.golioth.io/firmware/golioth-firmware-sdk/firmware-upgrade/firmware-upgrade)
for more info.

## Add Pipeline to Golioth

Golioth uses [Pipelines](https://docs.golioth.io/data-routing) to route
stream data. This gives you flexibility to change your data routing
without requiring updated device firmware.

Whenever sending stream data, you must enable a pipeline in your Golioth
project to configure how that data is handled. Add the contents of
`pipelines/json-to-lightdb.yml` as a new pipeline as follows (note that
this is the default pipeline for new projects and may already be
present):

1.  Navigate to your project on the Golioth web console.
2.  Select `Pipelines` from the left sidebar and click the `Create`
    button.
3.  Give your new pipeline a name and paste the pipeline configuration
    into the editor.
4.  Click the toggle in the bottom right to enable the pipeline and
    then click `Create`.

All data streamed to Golioth in JSON format will now be routed to
LightDB Stream and may be viewed using the web console. You may change
this behavior at any time without updating firmware simply by editing
this pipeline entry.

## Local set up

> [!IMPORTANT]
> Do not clone this repo using git. Zephyr's `west` meta tool should be
> used to set up your local workspace.

### Install the Python virtual environment (recommended)

``` shell
cd ~
mkdir golioth-reference-design-powermonitor
python -m venv golioth-reference-design-powermonitor/.venv
source golioth-reference-design-powermonitor/.venv/bin/activate
pip install wheel west ecdsa
```

### Use `west` to initialize and install

``` shell
cd ~/golioth-reference-design-powermonitor
west init -m git@github.com:golioth/reference-design-powermonitor.git .
west update
west zephyr-export
pip install -r deps/zephyr/scripts/requirements.txt
```

## Building the application

Build the Zephyr sample application for the [Nordic nRF9160
DK](https://www.nordicsemi.com/Products/Development-hardware/nrf9160-dk)
(`nrf9160dk_nrf9160_ns`) from the top level of your project. After a
successful build you will see a new `build` directory. Note that any
changes (and git commits) to the project itself will be inside the `app`
folder. The `build` and `deps` directories being one level higher
prevents the repo from cataloging all of the changes to the dependencies
and the build (so no `.gitignore` is needed).

Prior to building, update `VERSION` file to reflect the firmware version
number you want to assign to this build. Then run the following commands
to build and program the firmware onto the device.

> [!WARNING]
> You must perform a pristine build (use `-p` or remove the `build`
> directory) after changing the firmware version number in the `VERSION`
> file for the change to take effect.

``` text
$ (.venv) west build -p -b nrf9160dk/nrf9160/ns --sysbuild app
$ (.venv) west flash
```

Configure PSK-ID and PSK using the device shell based on your Golioth
credentials and reboot:

``` text
uart:~$ settings set golioth/psk-id <my-psk-id@my-project>
uart:~$ settings set golioth/psk <my-psk>
uart:~$ kernel reboot cold
```

## External Libraries

The following code libraries are installed by default. If you are not
using the custom hardware to which they apply, you can safely remove
these repositories from `west.yml` and remove the includes/function
calls from the C code.

  - [golioth-zephyr-boards](https://github.com/golioth/golioth-zephyr-boards)
    includes the board definitions for the Golioth Aludel-Mini
  - [libostentus](https://github.com/golioth/libostentus) is a helper
    library for controlling the Ostentus ePaper faceplate
  - [zephyr-network-info](https://github.com/golioth/zephyr-network-info)
    is a helper library for querying, formatting, and returning network
    connection information via Zephyr log or Golioth RPC

## Have Questions?

Please get in touch with Golioth engineers by starting a new thread on
the [Golioth Forum](https://forum.golioth.io/).
