
ssh chanhserver@chanhserver
scp chanhserver@chanhserver:/home/chanhserver/Tuya_Project/platform/ESP32/tuya_open_sdk/build/main_project_QIO_1.0.0.bin .
python -m esptool --chip esp32s3 --port COMx --baud 921600 write_flash 0x0 main_project_QIO_1.0.0.bin
idf.py monitor
