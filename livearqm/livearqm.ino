// Distributed with a free-will license.
// Use it any way you want, profit or free, provided it fits in the licenses of its associated works.
// ADXL345
// This code is designed to work with the ADXL345_I2CS I2C Mini Module available from ControlEverything.com.
//Adding functionality to send data to a local server.
// https://www.controleverything.com/content/Accelorometer?sku=ADXL345_I2CS#tabs-0-product_tabset-2

#include <application.h>
#include <spark_wiring_i2c.h>
#include "Particle.h"

SYSTEM_THREAD(ENABLED);

int devicesHandler(String data); // forward declaration
void sendData(void);
char* alphadate(int);


// ADXL345 I2C address is 0x53(83)
#define Addr 0x53
int count = 0;
int tmStamp;
int xAccl, yAccl, zAccl;


//Setup default times and such
const unsigned long REQUEST_WAIT_MS = 300;
const unsigned long RETRY_WAIT_MS = 1100;
const unsigned long SEND_WAIT_MS = 1;

enum State { STATE_REQUEST, STATE_REQUEST_WAIT, STATE_CONNECT, STATE_SEND_DATA, STATE_RETRY_WAIT };
State state = STATE_REQUEST;
unsigned long stateTime = 0;
IPAddress serverAddr;
int serverPort;
char nonce[34];
TCPClient client;

int nate = 0;

void setup()
{   
  // Set variables
  Particle.variable("i2cdevice","ADXL345");
  Particle.variable("xAccl",xAccl);
  Particle.variable("yAccl",yAccl);
  Particle.variable("zAccl",zAccl);
  
  // Set function (for server communication)
  Particle.function("devices", devicesHandler);
  
  // Initialise serial communication, set baud rate = 9600
  Serial.begin(9600);
  
  //Setting clock speed
  Wire.setSpeed(400000);
  // Initialise I2C communication as MASTER 
  Wire.begin();
  
  //Disabling single tap, double tap, active and freefall functions
  //Select INT Enable Register
  Wire.write(0x2E);
  //Write 0 to the DUR
  Wire.write(0x83);
  
  // Start I2C transmission
  Wire.beginTransmission(Addr);
  // Select bandwidth rate register
  Wire.write(0x2C);
  // Select output data rate = 800 Hz
  Wire.write(0x0D);
  // Stop I2C Transmission
  Wire.endTransmission();

  // Start I2C transmission
  Wire.beginTransmission(Addr);
  // Select power control register
  Wire.write(0x2D);
  // Select auto sleep disable
  Wire.write(0x08);
  // Stop I2C transmission
  Wire.endTransmission();

  // Start I2C transmission
  Wire.beginTransmission(Addr);
  // Select data format register
  Wire.write(0x31);
  // Select full resolution, +/-8g
  Wire.write(0x0A);
  //FIFO CTL
  Wire.write(0x38);
  //Make it stream mode
  Wire.write(0x90);
  // End I2C transmission
  Wire.endTransmission();
 
  delay(300);
}



void loop() {
	switch(state) {
    
	case STATE_REQUEST:
		if (Particle.connected()) {
			Serial.println("sending devicesRequest");
			Particle.publish("devicesRequest", WiFi.localIP().toString().c_str(), 10, PRIVATE);
			state = STATE_REQUEST_WAIT;
			stateTime = millis();
		}
		break;

	case STATE_REQUEST_WAIT:
		if (millis() - stateTime >= REQUEST_WAIT_MS) {
			state = STATE_RETRY_WAIT;
			stateTime = millis();
		}
		break;

	case STATE_CONNECT:
		if (client.connect(serverAddr, serverPort)) {
			client.println("POST /devices HTTP/1.0");
			client.printlnf("Authorization: %s", nonce);
			client.printlnf("Content-Length: 99999999");
		    client.println();
		    state = STATE_SEND_DATA;
		}
		else {
			state = STATE_RETRY_WAIT;
			stateTime = millis();
		}
		break;

	case STATE_SEND_DATA:
		// In this state, we send data until we lose the connection to the server for whatever
		// reason. We'll to the server again.
		if (!client.connected()) {
			Serial.println("server disconnected");
			client.stop();
			state = STATE_RETRY_WAIT;
			stateTime = millis();
			break;
		}

		if (millis() - stateTime >= SEND_WAIT_MS) {
			stateTime = millis();
			sendData();
		}
		break;

	case STATE_RETRY_WAIT:
		if (millis() - stateTime >= RETRY_WAIT_MS) {
			state = STATE_REQUEST;
		}
		break;
		
	}
	
}



void sendData(void) {
	// Called periodically when connected via TCP to the server to update data.
	// Unlike Particle.publish you can push a very large amount of data through this connection,
	// theoretically up to about 800 Kbytes/sec, but really you should probably shoot for something
	// lower than that, especially with the way connection is being served in the node.js server.

	// Taking values from an accelerometer to print to TCP client
	// Acceleration read on three axes and also a timeStamp is added
	
    // Read 6 bytes of data
    // xAccl lsb, xAccl msb, yAccl lsb, yAccl msb, zAccl lsb, zAccl msb
    unsigned int data[6];
    for(int i = 0; i < 6; i++)
    {
      // Start I2C transmission
      Wire.beginTransmission(Addr);
      // Select data register
      Wire.write((50+i));
      // Stop I2C transmission
      Wire.endTransmission();

      // Request 1 byte of data from the device
      Wire.requestFrom(Addr,1);
      if(Wire.available()==1) 
      {
        data[i] = Wire.read();
      }
    }

    // Use 12-bits if data
    xAccl = (((data[1] & 0x0F) * 256) + data[0]);
    if(xAccl > 2047)
    {
      xAccl -= 4096;
    }
    yAccl = (((data[3] & 0x0F) * 256) + data[2]);
    if(yAccl > 2047)
    {
      yAccl -= 4096;
    }
    zAccl = (((data[5] & 0x0F) * 256) + data[4]);
    if(zAccl > 2047)
    {
      zAccl -= 4096;
    }

    tmStamp = millis();
  
    Serial.printf("%d;    X:%d,Y:%d,Z:%d;    %d\n", tmStamp, xAccl, yAccl, zAccl, count);
    
    // Use printf and manually added a \n here. The server code splits on LF only, and using println/
	// printlnf adds both a CR and LF. It's easier to parse with LF only, and it saves a byte when
	// transmitting.
	client.printf("%d;    X:%d,Y:%d,Z:%d;    %d\n", tmStamp, xAccl, yAccl, zAccl, count);
    
    //print date and time stamp every second or so
    if(tmStamp % 1000 == 0)
    {
      Serial.printf("%d-%s-%d;    %d:%d:%d\n", Time.year(), alphadate(Time.month()), Time.day(), Time.hour(), Time.minute(), Time.second());
      client.printf("%d-%s-%d;    %d:%d:%d\n", Time.year(), alphadate(Time.month()), Time.day(), Time.hour(), Time.minute(), Time.second());
    }
	
	count++;
}


// This is the handler for the Particle.function "devices"
// The server makes this function call after this device publishes a devicesRequest event.
// The server responds with an IP address and port of the server, and a nonce (number used once) for authentication.
int devicesHandler(String data) {
	Serial.printlnf("devicesHandler data=%s", data.c_str());
	int addr[4];

	if (sscanf(data, "%u.%u.%u.%u,%u,%32s", &addr[0], &addr[1], &addr[2], &addr[3], &serverPort, nonce) == 6) {
		serverAddr = IPAddress(addr[0], addr[1], addr[2], addr[3]);
		Serial.printlnf("serverAddr=%s serverPort=%u nonce=%s", serverAddr.toString().c_str(), serverPort, nonce);
		state = STATE_CONNECT;
	}
	return 0;
}


char* alphadate(int m)
{
    switch(m)  {
    case 1: return "JAN";
    case 2: return "FEB";
    case 3: return "MAR";
    case 4: return "APR";
    case 5: return "MAY";
    case 6: return "JUN";
    case 7: return "JUL";
    case 8: return "AUG";
    case 9: return "SEP";
    case 10: return "OCT";
    case 11: return "NOV";
    case 12: return "DEC";
    default: return "";
    }
}
Contact GitHub 
