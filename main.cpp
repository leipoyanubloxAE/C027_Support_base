/*
 * PackageLicenseDeclared: Apache-2.0
 * Copyright (c) 2015 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mbed.h"
#include "C027_Support/SerialPipe.h"


DigitalOut led1(LED1);

SerialPipe mdm(MDMTXD,MDMRXD);
SerialPipe pc(USBTX, USBRX);

int main() {
    char cmd[10];
    char buf[10];

    printf("Done\r\n");
    
    mdm.baud(MDMBAUD);
    pc.baud(MDMBAUD);

    while (true) {
        led1 = !led1;
#if 0
        if (pc.readable() && mdm.writeable()) {
            mdm.putc(pc.getc());
        }
        if (mdm.readable() && pc.writeable()) {
            pc.putc(mdm.getc());
        }
#else
	memset(cmd, 0, sizeof(cmd));
	memset(buf, 0, sizeof(buf));

	sprintf(cmd, "AT+GMM\n");
        mdm.put(cmd, strlen(cmd), 1);
	mdm.get(buf, sizeof(buf), 0);
 	printf("mdm Buf is: %s.\n", strlen(buf) ? "NULL" : buf);

        Thread::wait(500);

#endif
    }
}


