/*
 * To-do:
 * - Analog support
 * - Some sort of connection control, it only sends packets
 *   but there is no acknoledgement of a connection o keepalives
 * - Send player name
 * - Render something on-screen maybe a gui to configure IP and port
     instead of the ridiculously long strings we're using now
 * - Allow changing IP address and port in runtime
 * - Support other platforms
 * - Input recording / Combos
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
 
#include <net/net_compat.h>
#include <net/net_socket.h>

#include <retro_miscellaneous.h>

#include <libretro.h>

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

#ifdef RARCH_INTERNAL
#include "internal_cores.h"
#define NETRETROPAD_CORE_PREFIX(s) libretro_netretropad_##s
#else
#define NETRETROPAD_CORE_PREFIX(s) s
#endif

static int s;
static int port;
static char server[64];
static struct sockaddr_in si_other;
 
static struct retro_log_callback logger;

static retro_log_printf_t NETRETROPAD_CORE_PREFIX(log_cb);
static retro_video_refresh_t NETRETROPAD_CORE_PREFIX(video_cb);
static retro_audio_sample_t NETRETROPAD_CORE_PREFIX(audio_cb);
static retro_audio_sample_batch_t NETRETROPAD_CORE_PREFIX(audio_batch_cb);
static retro_environment_t NETRETROPAD_CORE_PREFIX(environ_cb);
static retro_input_poll_t NETRETROPAD_CORE_PREFIX(input_poll_cb);
static retro_input_state_t NETRETROPAD_CORE_PREFIX(input_state_cb);

static uint16_t *frame_buf;

void NETRETROPAD_CORE_PREFIX(retro_init)(void)
{
   frame_buf = (uint16_t*)calloc(320 * 240, sizeof(uint16_t));

   NETRETROPAD_CORE_PREFIX(log_cb)(RETRO_LOG_INFO, "Initialising sockets...\n");
   network_init();
}

void NETRETROPAD_CORE_PREFIX(retro_deinit)(void)
{
   if (frame_buf)
      free(frame_buf);
   frame_buf = NULL;
}

unsigned NETRETROPAD_CORE_PREFIX(retro_api_version)(void)
{
   return RETRO_API_VERSION;
}

void NETRETROPAD_CORE_PREFIX(retro_set_controller_port_device)(
      unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}

void NETRETROPAD_CORE_PREFIX(retro_get_system_info)(
      struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "RetroPad Remote";
   info->library_version  = "0.01";
   info->need_fullpath    = false;
   info->valid_extensions = ""; /* Nothing. */
}

void NETRETROPAD_CORE_PREFIX(retro_get_system_av_info)(
      struct retro_system_av_info *info)
{
   info->timing.fps = 60.0;
   info->timing.sample_rate = 30000.0;

   info->geometry.base_width  = 320;
   info->geometry.base_height = 240;
   info->geometry.max_width   = 320;
   info->geometry.max_height  = 240;
   info->geometry.aspect_ratio = 4.0 / 3.0;
}


static unsigned retropad_update_input(void)
{
   unsigned value = 0;
   NETRETROPAD_CORE_PREFIX(input_poll_cb)();

   if (NETRETROPAD_CORE_PREFIX(input_state_cb)(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))
      value += 1;
   if (NETRETROPAD_CORE_PREFIX(input_state_cb)(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y))
      value += 2;
   if (NETRETROPAD_CORE_PREFIX(input_state_cb)(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT))
      value += pow(2, 2);
   if (NETRETROPAD_CORE_PREFIX(input_state_cb)(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START))
      value += pow(2, 3);
   if (NETRETROPAD_CORE_PREFIX(input_state_cb)(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))
      value += pow(2, 4);
   if (NETRETROPAD_CORE_PREFIX(input_state_cb)(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))
      value += pow(2, 5);
   if (NETRETROPAD_CORE_PREFIX(input_state_cb)(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))
      value += pow(2, 6);
   if (NETRETROPAD_CORE_PREFIX(input_state_cb)(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))
      value += pow(2, 7);
   if (NETRETROPAD_CORE_PREFIX(input_state_cb)(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A))
      value += pow(2, 8);
   if (NETRETROPAD_CORE_PREFIX(input_state_cb)(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X))
      value += pow(2, 9);
   if (NETRETROPAD_CORE_PREFIX(input_state_cb)(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L))
      value += pow(2, 10);
   if (NETRETROPAD_CORE_PREFIX(input_state_cb)(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R))
      value += pow(2, 11);

   return value;
}

void NETRETROPAD_CORE_PREFIX(retro_set_environment)(retro_environment_t cb)
{
   static const struct retro_variable vars[] = {
      { "net_retropad_port", "Port; 55400|55401|55402|55403|55404|55405|55406|55407|55408|55409|55410|55411|55412|55413|55414|55415|55416|55417|55418|55419|55420" },
      { "net_retropad_ip_octet1", "IP address part 1; 0|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|19|20|21|22|23|24|25|26|27|28|29|30|31|32|33|34|35|36|37|38|39|40|41|42|43|44|45|46|47|48|49|50|51|52|53|54|55|56|57|58|59|60|61|62|63|64|65|66|67|68|69|70|71|72|73|74|75|76|77|78|79|80|81|82|83|84|85|86|87|88|89|90|91|92|93|94|95|96|97|98|99|100|101|102|103|104|105|106|107|108|109|110|111|112|113|114|115|116|117|118|119|120|121|122|123|124|125|126|127|128|129|130|131|132|133|134|135|136|137|138|139|140|141|142|143|144|145|146|147|148|149|150|151|152|153|154|155|156|157|158|159|160|161|162|163|164|165|166|167|168|169|170|171|172|173|174|175|176|177|178|179|180|181|182|183|184|185|186|187|188|189|190|191|192|193|194|195|196|197|198|199|200|201|202|203|204|205|206|207|208|209|210|211|212|213|214|215|216|217|218|219|220|221|222|223|224|225|226|227|228|229|230|231|232|233|234|235|236|237|238|239|240|241|242|243|244|245|246|247|248|249|250|251|252|253|254|255" },
      { "net_retropad_ip_octet2", "IP address part 2; 0|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|19|20|21|22|23|24|25|26|27|28|29|30|31|32|33|34|35|36|37|38|39|40|41|42|43|44|45|46|47|48|49|50|51|52|53|54|55|56|57|58|59|60|61|62|63|64|65|66|67|68|69|70|71|72|73|74|75|76|77|78|79|80|81|82|83|84|85|86|87|88|89|90|91|92|93|94|95|96|97|98|99|100|101|102|103|104|105|106|107|108|109|110|111|112|113|114|115|116|117|118|119|120|121|122|123|124|125|126|127|128|129|130|131|132|133|134|135|136|137|138|139|140|141|142|143|144|145|146|147|148|149|150|151|152|153|154|155|156|157|158|159|160|161|162|163|164|165|166|167|168|169|170|171|172|173|174|175|176|177|178|179|180|181|182|183|184|185|186|187|188|189|190|191|192|193|194|195|196|197|198|199|200|201|202|203|204|205|206|207|208|209|210|211|212|213|214|215|216|217|218|219|220|221|222|223|224|225|226|227|228|229|230|231|232|233|234|235|236|237|238|239|240|241|242|243|244|245|246|247|248|249|250|251|252|253|254|255" },
      { "net_retropad_ip_octet3", "IP address part 3; 0|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|19|20|21|22|23|24|25|26|27|28|29|30|31|32|33|34|35|36|37|38|39|40|41|42|43|44|45|46|47|48|49|50|51|52|53|54|55|56|57|58|59|60|61|62|63|64|65|66|67|68|69|70|71|72|73|74|75|76|77|78|79|80|81|82|83|84|85|86|87|88|89|90|91|92|93|94|95|96|97|98|99|100|101|102|103|104|105|106|107|108|109|110|111|112|113|114|115|116|117|118|119|120|121|122|123|124|125|126|127|128|129|130|131|132|133|134|135|136|137|138|139|140|141|142|143|144|145|146|147|148|149|150|151|152|153|154|155|156|157|158|159|160|161|162|163|164|165|166|167|168|169|170|171|172|173|174|175|176|177|178|179|180|181|182|183|184|185|186|187|188|189|190|191|192|193|194|195|196|197|198|199|200|201|202|203|204|205|206|207|208|209|210|211|212|213|214|215|216|217|218|219|220|221|222|223|224|225|226|227|228|229|230|231|232|233|234|235|236|237|238|239|240|241|242|243|244|245|246|247|248|249|250|251|252|253|254|255" },
      { "net_retropad_ip_octet4", "IP address part 4; 0|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|19|20|21|22|23|24|25|26|27|28|29|30|31|32|33|34|35|36|37|38|39|40|41|42|43|44|45|46|47|48|49|50|51|52|53|54|55|56|57|58|59|60|61|62|63|64|65|66|67|68|69|70|71|72|73|74|75|76|77|78|79|80|81|82|83|84|85|86|87|88|89|90|91|92|93|94|95|96|97|98|99|100|101|102|103|104|105|106|107|108|109|110|111|112|113|114|115|116|117|118|119|120|121|122|123|124|125|126|127|128|129|130|131|132|133|134|135|136|137|138|139|140|141|142|143|144|145|146|147|148|149|150|151|152|153|154|155|156|157|158|159|160|161|162|163|164|165|166|167|168|169|170|171|172|173|174|175|176|177|178|179|180|181|182|183|184|185|186|187|188|189|190|191|192|193|194|195|196|197|198|199|200|201|202|203|204|205|206|207|208|209|210|211|212|213|214|215|216|217|218|219|220|221|222|223|224|225|226|227|228|229|230|231|232|233|234|235|236|237|238|239|240|241|242|243|244|245|246|247|248|249|250|251|252|253|254|255" },

      { NULL, NULL },
   };
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);


   NETRETROPAD_CORE_PREFIX(environ_cb) = cb;
   bool no_content = true;
   cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

   NETRETROPAD_CORE_PREFIX(environ_cb)(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);

   if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logger))
      NETRETROPAD_CORE_PREFIX(log_cb) = logger.log;
}

static void netretropad_check_variables(void)
{
   struct retro_variable var, var2, var3, var4, port_var;
   var.key      = "net_retropad_ip_octet1";
   var2.key     = "net_retropad_ip_octet2";
   var3.key     = "net_retropad_ip_octet3";
   var4.key     = "net_retropad_ip_octet4";
   port_var.key = "net_retropad_port";

   NETRETROPAD_CORE_PREFIX(environ_cb)(RETRO_ENVIRONMENT_GET_VARIABLE, &var);
   NETRETROPAD_CORE_PREFIX(environ_cb)(RETRO_ENVIRONMENT_GET_VARIABLE, &var2);
   NETRETROPAD_CORE_PREFIX(environ_cb)(RETRO_ENVIRONMENT_GET_VARIABLE, &var3);
   NETRETROPAD_CORE_PREFIX(environ_cb)(RETRO_ENVIRONMENT_GET_VARIABLE, &var4);
   NETRETROPAD_CORE_PREFIX(environ_cb)(RETRO_ENVIRONMENT_GET_VARIABLE, &port_var);

   snprintf(server, sizeof(server), "%s.%s.%s.%s", var.value, var2.value, var3.value, var4.value);
   port = atoi(port_var.value);
}

void NETRETROPAD_CORE_PREFIX(retro_set_audio_sample)(retro_audio_sample_t cb)
{
   NETRETROPAD_CORE_PREFIX(audio_cb) = cb;
}

void NETRETROPAD_CORE_PREFIX(retro_set_audio_sample_batch)(
      retro_audio_sample_batch_t cb)
{
   NETRETROPAD_CORE_PREFIX(audio_batch_cb) = cb;
}

void NETRETROPAD_CORE_PREFIX(retro_set_input_poll)(retro_input_poll_t cb)
{
   NETRETROPAD_CORE_PREFIX(input_poll_cb) = cb;
}

void NETRETROPAD_CORE_PREFIX(retro_set_input_state)(retro_input_state_t cb)
{
   NETRETROPAD_CORE_PREFIX(input_state_cb) = cb;
}

void NETRETROPAD_CORE_PREFIX(retro_set_video_refresh)(retro_video_refresh_t cb)
{
   NETRETROPAD_CORE_PREFIX(video_cb) = cb;
}

void NETRETROPAD_CORE_PREFIX(retro_reset)(void)
{}

void NETRETROPAD_CORE_PREFIX(retro_run)(void)
{
   unsigned i;
   char message[64];
   unsigned input_state = retropad_update_input();

   if (input_state > 0)
   {
      snprintf(message, sizeof(message), "%d", input_state);

      /* send the message */
      if (sendto(s, message, strlen(message) , 0 , (struct sockaddr *) &si_other, sizeof(si_other))==-1)
          NETRETROPAD_CORE_PREFIX(log_cb)(RETRO_LOG_INFO, "Error sending data\n");
   }
   for (i = 0; i < 320 * 240; i++)
      frame_buf[i] = 4 << 5;
   NETRETROPAD_CORE_PREFIX(video_cb)(frame_buf, 320, 240, 640);

   retro_sleep(4);
}

bool NETRETROPAD_CORE_PREFIX(retro_load_game)(const struct retro_game_info *info)
{
   socket_target_t in_target;

   netretropad_check_variables();

   s = socket_create(
         "retropad",
         SOCKET_DOMAIN_INET,
         SOCKET_TYPE_DATAGRAM,
         SOCKET_PROTOCOL_UDP);

   if (s == SOCKET_ERROR)
      NETRETROPAD_CORE_PREFIX(log_cb)(RETRO_LOG_INFO, "socket failed");

   /* setup address structure */
   memset((char *) &si_other, 0, sizeof(si_other));

   in_target.port   = port;
   in_target.server = server;
   in_target.domain = SOCKET_DOMAIN_INET;

   socket_set_target(&si_other, &in_target); 

   NETRETROPAD_CORE_PREFIX(log_cb)(RETRO_LOG_INFO, "Server IP Address: %s\n" , server);

   return true;
}

void NETRETROPAD_CORE_PREFIX(retro_unload_game)(void)
{}

unsigned NETRETROPAD_CORE_PREFIX(retro_get_region)(void)
{
   return RETRO_REGION_NTSC;
}

bool NETRETROPAD_CORE_PREFIX(retro_load_game_special)(unsigned type,
      const struct retro_game_info *info, size_t num)
{
   (void)type;
   (void)info;
   (void)num;
   return false;
}

size_t NETRETROPAD_CORE_PREFIX(retro_serialize_size)(void)
{
   return 0;
}

bool NETRETROPAD_CORE_PREFIX(retro_serialize)(void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

bool NETRETROPAD_CORE_PREFIX(retro_unserialize)(const void *data,
      size_t size)
{
   (void)data;
   (void)size;
   return false;
}

void *NETRETROPAD_CORE_PREFIX(retro_get_memory_data)(unsigned id)
{
   (void)id;
   return NULL;
}

size_t NETRETROPAD_CORE_PREFIX(retro_get_memory_size)(unsigned id)
{
   (void)id;
   return 0;
}

void NETRETROPAD_CORE_PREFIX(retro_cheat_reset)(void)
{}

void NETRETROPAD_CORE_PREFIX(retro_cheat_set)(unsigned idx,
      bool enabled, const char *code)
{
   (void)idx;
   (void)enabled;
   (void)code;
}


