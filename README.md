
ssh chanhserver@chanhserver
scp chanhserver@chanhserver:/home/chanhserver/Tuya_product/.build/bin/Tuya_product_QIO_1.0.0.bin .
python -m esptool --chip esp32 --port COM4 --baud 921600 write_flash 0x0 Tuya_product_QIO_1.0.0.bin
idf.py monitor --port COM4


/home/chanhserver/TuyaOpen/platform/ESP32/tuya_open_sdk/tuyaos_adapter/**, /home/chanhserver/TuyaOpen/src/**