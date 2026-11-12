#ifndef BLE_CONTROLLER_H
#define BLE_CONTROLLER_H

/* Configure GAP/GATT after nimble_port_init(), before starting the host task. */
void ble_controller_init(void);

#endif
