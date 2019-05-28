# nRF5 Repo Notes

## Building

Flash gateway

```
mkdir build_52840 && cd build_52840
cmake -G Ninja -DPLATFORM=nrf52840_xxAA ..
ninja flash_apps_gateway_nrf52840_xxAA_s140_6.1.0
```

Flash endpoint

```
mkdir build_52832 && cd build_52832
cmake -G Ninja -DPLATFORM=nrf52832_xxAA ..
ninja flash_apps_endpoint_nrf52832_xxAA_s132_6.1.0
```

Flash friend

```
cd zephyr/

# Do Zephyr environment setup...
source zephyr-env.sh

export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
export GNUARMEMB_TOOLCHAIN_PATH=/usr/local

export PATH=/opt/nrfjprog:$PATH

# Build
cmake -GNinja -DBOARD=nrf52840_pca10059 ..
nrfjprog --program zephyr/zephyr.hex -f nrf52

```