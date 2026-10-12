void open_serial(int speed);
int read_serial();
int write_serial(int data);
int write_serial_free();

void open_serial2(int speed);
int read_serial2();
int write_serial2(int data);
int write_serial2_free();

void open_serial3(int speed);
int read_serial3();
int write_serial3(int data);
int write_serial3_free();

void open_serial4(int speed);
int read_serial4();
int write_serial4(int data);
int write_serial4_free();

void open_civ(int speed);
int read_civ();
int write_civ(int data);
int write_civ_free();

int open_serialUSB(int speed, char *idserial, char *idVendor);
int read_serialUSB(int id);
int write_serialUSB(int id, int data);
int write_serialUSB_free(int id);
