/*
Copyright (c) 2017, Intel Corporation
Copyright (c) 2017, Facebook Inc.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "pin_interface.h"
#include "SoftwareJTAGHandler.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "openbmc/pal.h"
#include "openbmc/gpio.h"
#include <facebook/bic.h>

#define FBY2_DEBUG
#define SOCK_PATH_ASD_BIC "/tmp/asd_bic_socket"


//  PLTRST,  PRDY, XDP_PRESENT
enum  MONITOR_EVENTS {
  JTAG_PLTRST_EVENT   = 0,
  JTAG_PRDY_EVENT,
  JTAG_XDP_PRESENT_EVENT,
  JTAG_EVENT_NUM,
};
static bool g_gpios_triggered[JTAG_EVENT_NUM] = {false, false, false};


static pthread_mutex_t triggered_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t poll_thread;

static void *gpio_poll_thread(void *arg);

int pin_initialize(const int fru)
{
    static bool gpios_polling = false;
    int *arg;

#ifdef FBY2_DEBUG
    syslog(LOG_DEBUG, "%s, fru=%d", __FUNCTION__, fru);
#endif
    if ((fru < FRU_SLOT1) || (fru > FRU_SLOT4)) {
      syslog(LOG_ERR, "%s: invalid fru: %d", __FUNCTION__, fru);
      return ST_ERR;
    }

    arg = malloc(sizeof(arg));
    if (arg == NULL) {
      syslog(LOG_ERR, "%s: malloc failed, fru=%d", __FUNCTION__, fru);
      return ST_ERR;
    }
    *arg = fru;

    /* Platform specific enables which are required for the ASD feature */

    /* needs to
     *     - enable DEBUG_EN
     *     - enable POWER_DEBUG_EN  (XDP_BIC_PWR_DEBUG_N?)
     *     - FM_JTAG_BIC_TCK_MUX_SEL_N  (select ?)
     *
     *     starts 1 thread monitoring GPIO pins
     *         PLTRST_BUF_N - both edge
     *         PRDY   - fallign edge
     *         XDP_PRSNT_IN_N - both edges   (if triggered, disable asd)
     */

    /* Start the GPIO polling threads just once */
    if (gpios_polling == false) {
        pthread_create(&poll_thread, NULL, gpio_poll_thread, arg);
        gpios_polling = true;
    } else {
        pthread_mutex_lock(&triggered_mutex);
        g_gpios_triggered[JTAG_PLTRST_EVENT] = false;
        g_gpios_triggered[JTAG_PRDY_EVENT] = false;
        g_gpios_triggered[JTAG_XDP_PRESENT_EVENT] = false;
        pthread_mutex_unlock(&triggered_mutex);
    }
    return ST_OK;
}

int pin_deinitialize(const int fru)
{
#ifdef FBY2_DEBUG
    syslog(LOG_DEBUG, "%s, fru=%d", __FUNCTION__, fru);
#endif

    if ((fru < FRU_SLOT1) || (fru > FRU_SLOT4)) {
      syslog(LOG_ERR, "%s: invalid fru: %d", __FUNCTION__, fru);
      return ST_ERR;
    }

    // TBD

    return ST_OK;
}


int power_debug_assert(const int fru, const bool assert)
{
#ifdef FBY2_DEBUG
    syslog(LOG_DEBUG, "%s, fru=%d, assert=%d", __FUNCTION__, fru, assert);
#endif

    if ((fru < FRU_SLOT1) || (fru > FRU_SLOT4)) {
      syslog(LOG_ERR, "%s: invalid fru: %d", __FUNCTION__, fru);
      return ST_ERR;
    }

#if 0
    /* Active low. */
    gpio_write(&power_debug_en_gpio, assert ? GPIO_VALUE_LOW : GPIO_VALUE_HIGH);
#endif

    return ST_OK;
}

int power_debug_is_asserted(const int fru, bool* asserted)
{
#if 0
    gpio_value_en value;
    if ((fru < FRU_SLOT1) || (fru > FRU_SLOT4)) {
      printf("%s: invalid fru: %d", __FUNCTION__, fru);
      return ST_ERR;
    }
    value = gpio_read(&power_debug_en_gpio);
    if (value == GPIO_VALUE_INVALID) {
      return ST_ERR;
    }
    *asserted = value == GPIO_VALUE_LOW ? true : false;
#else
    *asserted = false;
#endif

#ifdef FBY2_DEBUG
    if (*asserted)
      syslog(LOG_DEBUG, "%s, fru=%d asserted", __FUNCTION__, fru);
#endif

    return ST_OK;
}

int preq_assert(const int fru, const bool assert)
{
    int ret;

#ifdef FBY2_DEBUG
    syslog(LOG_DEBUG, "%s, fru=%d, assert=%d", __FUNCTION__, fru, assert);
#endif

    if ((fru < FRU_SLOT1) || (fru > FRU_SLOT4)) {
      syslog(LOG_ERR, "%s: invalid fru: %d", __FUNCTION__, fru);
      return ST_ERR;
    }

    /* active low */
    ret = bic_set_gpio(fru, XDP_BIC_PREQ_N, assert ? GPIO_VALUE_LOW : GPIO_VALUE_HIGH);

    return ret;
}

int preq_is_asserted(const int fru, bool* asserted)
{
    bic_gpio_t gpio;
    int ret;

    if ((fru < FRU_SLOT1) || (fru > FRU_SLOT4)) {
      syslog(LOG_ERR, "%s: invalid fru: %d", __FUNCTION__, fru);
      return ST_ERR;
    }

    ret = bic_get_gpio(fru, &gpio);
    if (!ret) {
      /* active low */
      *asserted = gpio.xdp_bic_preq_n == GPIO_VALUE_LOW ? true : false;
    }

#ifdef FBY2_DEBUG
    if (*asserted)
      syslog(LOG_DEBUG, "%s, fru=%d asserted", __FUNCTION__, fru);
#endif

    return ret;
}



// open a socket and waits for communcation from ipmid
static void *gpio_poll_thread(void *fru)
{
  int sock, msgsock, n, len, gpio_pin, ret=0;
  size_t t;
  struct sockaddr_un server, client;
  unsigned char req_buf[256];
  char sock_path[64] = {0};

  if ((sock = socket (AF_UNIX, SOCK_STREAM, 0)) == -1)
  {
    syslog(LOG_ERR, "ASD_BIC: socket() failed\n");
    exit (1);
  }

  server.sun_family = AF_UNIX;
  sprintf(sock_path, "%s_%d", SOCK_PATH_ASD_BIC, *(int *)fru);
  strcpy(server.sun_path, sock_path);
  unlink (server.sun_path);
  len = strlen (server.sun_path) + sizeof (server.sun_family);
  if (bind (sock, (struct sockaddr *) &server, len) == -1)
  {
    syslog(LOG_ERR, "ASD_BIC: bind() failed, errno=%d", errno);
    exit (1);
  }
  syslog(LOG_DEBUG, "ASD_BIC: Socket has name %s", server.sun_path);
  if (listen (sock, 5) == -1)
  {
    syslog(LOG_ERR, "ASD_BIC: listen() failed, errno=%d", errno);
    exit (1);
  }

  while (1) {
    t = sizeof (client);
    if ((msgsock = accept(sock, (struct sockaddr *) &client, &t)) < 0) {
      ret = errno;
      syslog(LOG_WARNING, "ASD_BIC: accept() failed with ret: %x, errno: %x\n",
             msgsock, ret);
      sleep(1);
      continue;
    }

    n = recv(msgsock, req_buf, sizeof(req_buf), 0);
    if (n <= 0) {
        syslog(LOG_WARNING, "ASD_BIC: recv() failed with %d\n", n);
        sleep(1);
        continue;
    } else {
        syslog(LOG_DEBUG, "message received, %d %d", req_buf[0], req_buf[1]);

        gpio_pin = req_buf[0];
        pthread_mutex_lock(&triggered_mutex);
        switch (gpio_pin) {
          case PLTRST_N:
            syslog(LOG_DEBUG, "ASD_BIC: PLTRST_N event");
            g_gpios_triggered[JTAG_PLTRST_EVENT] = true;;
            break;
/*   ToDo: these 2 pins hae not been defined yet in BIC spec */
/*
          case  PRDY:
            syslog(LOG_DEBUG, "ASD_BIC: PRDY event");
            g_gpios_triggered[JTAG_PRDY_EVENT] = true;;
            break;
          case  XDP_PRSNT_IN:
            syslog(LOG_DEBUG, "ASD_BIC: XDP_PRESENT event");
            g_gpios_triggered[JTAG_XDP_PRESENT_EVENT] = true;;
            break;
*/
          default:
            syslog(LOG_ERR, "ASD BIC: unknown GPIO pin # received, %d", gpio_pin);
        }
        pthread_mutex_unlock(&triggered_mutex);
    }
  }

  return NULL;
}


int platform_reset_is_event_triggered(const int fru, bool* triggered)
{
    if ((fru < FRU_SLOT1) || (fru > FRU_SLOT4)) {
      syslog(LOG_ERR, "%s: invalid fru: %d", __FUNCTION__, fru);
      return ST_ERR;
    }
    pthread_mutex_lock(&triggered_mutex);
    *triggered = g_gpios_triggered[JTAG_PLTRST_EVENT];
    g_gpios_triggered[JTAG_PLTRST_EVENT] = false;
    pthread_mutex_unlock(&triggered_mutex);
    /* TODO */
    *triggered = false;

#ifdef FBY2_DEBUG
    if (*triggered)
      syslog(LOG_DEBUG, "%s fru=%d, triggered", __FUNCTION__, fru);
#endif

    return ST_OK;
}

int platform_reset_is_asserted(const int fru, bool* asserted)
{
    bic_gpio_t gpio;
    int ret;

    if ((fru < FRU_SLOT1) || (fru > FRU_SLOT4)) {
      syslog(LOG_ERR, "%s: invalid fru: %d", __FUNCTION__, fru);
      return ST_ERR;
    }

    ret = bic_get_gpio(fru, &gpio);
    if (!ret) {
      /* active low */
      *asserted = gpio.pltrst_n == GPIO_VALUE_LOW ? true : false;
    }

#ifdef FBY2_DEBUG
    if (*asserted)
      syslog(LOG_DEBUG, "%s fru=%d asserted", __FUNCTION__, fru);
#endif

    return ST_OK;
}



int prdy_is_event_triggered(const int fru, bool* triggered)
{
    if ((fru < FRU_SLOT1) || (fru > FRU_SLOT4)) {
      syslog(LOG_ERR, "%s: invalid fru: %d", __FUNCTION__, fru);
      return ST_ERR;
    }

    pthread_mutex_lock(&triggered_mutex);
    *triggered = g_gpios_triggered[JTAG_PRDY_EVENT];
    g_gpios_triggered[JTAG_PRDY_EVENT] = false;
    pthread_mutex_unlock(&triggered_mutex);
    /* TODO */

#ifdef FBY2_DEBUG
    if (*triggered)
      syslog(LOG_DEBUG, "%s fru=%d triggered", __FUNCTION__, fru);
#endif

    return ST_OK;
}

int prdy_is_asserted(const int fru, bool* asserted)
{
    bic_gpio_t gpio;
    int ret;

    if ((fru < FRU_SLOT1) || (fru > FRU_SLOT4)) {
      syslog(LOG_ERR, "%s: invalid fru: %d", __FUNCTION__, fru);
      return ST_ERR;
    }

    ret = bic_get_gpio(fru, &gpio);
    if (!ret) {
      /* active low */
      // todo - current BIC does not return PRDY pin to BMC
      //*asserted = gpio.prdy == GPIO_VALUE_LOW ? true : false;
    }

#ifdef FBY2_DEBUG
    if (*asserted)
      syslog(LOG_DEBUG, "%s fru=%d asserted", __FUNCTION__, fru);
#endif

    return ST_OK;
}

int xdp_present_is_event_triggered(const int fru, bool* triggered)
{
    if ((fru < FRU_SLOT1) || (fru > FRU_SLOT4)) {
      syslog(LOG_ERR, "%s: invalid fru: %d", __FUNCTION__, fru);
      return ST_ERR;
    }
    /* TODO */
    pthread_mutex_lock(&triggered_mutex);
    *triggered = g_gpios_triggered[JTAG_XDP_PRESENT_EVENT];
    g_gpios_triggered[JTAG_XDP_PRESENT_EVENT] = false;
    pthread_mutex_unlock(&triggered_mutex);

#ifdef FBY2_DEBUG
    if (*triggered)
      syslog(LOG_DEBUG, "%s fru=%d triggered", __FUNCTION__, fru);
#endif

    return ST_OK;
}

int xdp_present_is_asserted(const int fru, bool* asserted)
{
#if 0
    gpio_value_en value;
    if ((fru < FRU_SLOT1) || (fru > FRU_SLOT4)) {
      syslog(LOG_ERR, "%s: invalid fru: %d", __FUNCTION__, fru);
      return NULL;
    }
    value = gpio_read(xdp_present_gpio);
    if (value == GPIO_VALUE_INVALID) {
      return ST_ERR;
    }
    /* active-low */
    *asserted = value == GPIO_VALUE_LOW ? true : false;
#else
    *asserted = false;
#endif

#ifdef FBY2_DEBUG
    if (*asserted)
      syslog(LOG_DEBUG, "%s fru=%d asserted", __FUNCTION__, fru);
#endif

    return ST_OK;
}
