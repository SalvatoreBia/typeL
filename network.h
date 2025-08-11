#ifndef NETWORK_H
#define NETWORK_H


#ifdef __cplusplus
extern "C" {
#endif

void* handle_client(void *arg);
void* session_countdown(void *arg);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_H
