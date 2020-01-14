/*
 * Copyright (c) 2018 Martin Jäger / Libre Solar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "config.h"
#include "thingset.h"
#include "ext.h"

#ifdef __MBED__

#include "mbed.h"

#elif defined(__ZEPHYR__)

#include <zephyr.h>
#include <sys/printk.h>
#include <console/console.h>
#include <string.h>
#include <stdio.h>

#endif /* MBED or ZEPHYR */

class ThingSetStream: public ExtInterface
{
    public:
        #ifdef __MBED__
        ThingSetStream(Stream& s, const unsigned int c): channel(c), stream(&s) {};
        #elif defined(__ZEPHYR__)
        ThingSetStream(const unsigned int c): channel(c){};
        #endif

        virtual void process_asap();
        virtual void process_1s();

    protected:
        virtual void process_input(); // this is called from the ISR typically
        const unsigned int channel;

        virtual bool readable()
        {
            #ifdef __MBED__
            return stream->readable();
            #elif defined(__ZEPHYR__)
            return true;
            #endif
        }

    private:
        #ifdef __MBED__
        Stream* stream;
        #endif

        static char buf_resp[1000];           // only one response buffer needed for all objects
        char buf_req[500];
        size_t req_pos = 0;
        bool command_flag = false;
};

#ifdef __MBED__
template<typename T> class ThingSetSerial: public ThingSetStream
{
    public:
        ThingSetSerial(T& s, const unsigned int c): ThingSetStream(s,c), ser(s) {}

        void enable() {
            Callback<void()>  cb([this]() -> void { this->process_input();});
            ser.attach(cb);
        }

    private:
        bool readable()
        {
            return ser.readable();
        }

    private:
        T& ser;
};

#elif defined(__ZEPHYR__)
class ThingSetSerial: public ThingSetStream
{
    public:
        ThingSetSerial(const int c): ThingSetStream(c){}

        void enable() {
            console_init(); 
        }

    private:
        bool readable()
        {
            return true;
        }
};
#endif /* MBED or ZEPHYR*/

/*
 * Construct all global ExtInterfaces here.
 * All of these are added to the list of devices
 * for later processing in the normal operation
 */

#ifdef UART_SERIAL_ENABLED

    #ifdef __MBED__
        extern const int pub_channel_serial;
        extern Serial serial;
        ThingSetSerial ts_uart(serial, pub_channel_serial);

    #elif defined(__ZEPHYR__)
        extern const int pub_channel_serial;
        ThingSetSerial ts_uart(pub_channel_serial);
    #endif /* MBED or ZEPHYR */

#endif /* UART_SERIAL_ENABLED */

// ToDo: Add USB serial again (previous library was broken with recent mbed releases)

char ThingSetStream::buf_resp[1000];

extern ThingSet ts;

void ThingSetStream::process_1s()
{
    if (ts.get_pub_channel(channel)->enabled) {
        #ifdef __MBED__
        ts.pub_msg_json(buf_resp, sizeof(buf_resp), channel);
        stream->puts(buf_resp);
        stream->putc('\n');

        #elif defined(__ZEPHYR__)
        int len = ts.pub_msg_json(buf_resp, sizeof(buf_resp), channel);
        for (int i = 0; i < len; i++) {
            console_putchar(buf_resp[i]);
        }
        console_putchar('\n');
        #endif

    }
}

void ThingSetStream::process_asap()
{
    process_input();

    if (command_flag) {
        // commands must have 2 or more characters
        if (req_pos > 1) {
            #ifdef __MBED__
            stream->printf("Received Request (%d bytes): %s\n", strlen(buf_req), buf_req);
            ts.process((uint8_t *)buf_req, strlen(buf_req), (uint8_t *)buf_resp, sizeof(buf_resp));
            stream->puts(buf_resp);
            stream->putc('\n');
            fflush(*stream);

            #elif defined(__ZEPHYR__)
            printf("Received Request (%d bytes): %s\n", strlen(buf_req), buf_req);
            int len = ts.process((uint8_t *)buf_req, strlen(buf_req), (uint8_t *)buf_resp, sizeof(buf_resp));
            for (int i = 0; i < len; i++) {
                console_putchar(buf_resp[i]);
            }
            console_putchar('\n');
            #endif /* MBED or ZEPHYR */
        }

        // start listening for new commands
        command_flag = false;
        req_pos = 0;
    }
}

/**
 * Read characters from stream until line end \n detected, signal command available then
 * and wait for processing
 */
void ThingSetStream::process_input()
{
    while (readable() && command_flag == false) {

        #ifdef __MBED__
        int c = stream->getc();
        #elif defined(__ZEPHYR__)
        int c = console_getchar();
        #endif /* MBED or ZEPHYR */

        // \r\n and \n are markers for line end, i.e. command end
        // we accept this at any time, even if the buffer is 'full', since
        // there is always one last character left for the \0
        if (c == '\n') {
            if (req_pos > 0 && buf_req[req_pos-1] == '\r') {
                buf_req[req_pos-1] = '\0';
            }
            else {
                buf_req[req_pos] = '\0';
            }
            // start processing
            command_flag = true;
        }

        // backspace
        // can be done always unless there is nothing in the buffer
        else if (req_pos > 0 && c == '\b') {
            req_pos--;
        }
        // we fill the buffer up to all but 1 character
        // the last character is reserved for '\0'
        // more read characters are simply dropped, unless it is \n
        // which ends the command input and triggers processing
        else if (req_pos < (sizeof(buf_req)-1)) {
            buf_req[req_pos++] = c;
        }
    }
}
