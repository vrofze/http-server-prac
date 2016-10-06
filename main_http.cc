#include "server_http.hpp"
#include "handler.hpp"

using namespace FrostWeb;

int main()
{
  Server<HTTP> server(8089, 4);
  start_server<Server<HTTP>>(server, 8089);
  return 0;
}
