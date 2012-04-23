//
//  net.c
//  server
//
//  Created by Joseph Gentle on 12/04/12.
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//


#include <assert.h>
#include <stdio.h>
#include "net.h"
#include "kvec.h"
#include "lua.h"


typedef struct {
  uint32_t size;
  uint8_t type;
} Header;

void write_cb(uv_write_t *req, int status) {
  //  printf("Snapshot written!\n");
  free(req);
  
  // Actually we should free all the buffers here as well.
}

void send_bytes(Client *client, char type, const char *data, size_t len) {
  uv_buf_t buf = uv_buf_init((char *)malloc(len + 5), len + 5);
  *((uint32_t *)buf.base) = len + 1;
  *(buf.base + 4) = type;
  memcpy(buf.base + 5, data, len);
  uv_write_t *req = (uv_write_t *)malloc(sizeof(uv_write_t));
  uv_write(req, client->stream, &buf, 1, write_cb);
}

LUA_EXPORT void send_to_client(Client *client, const char *message, size_t len) {
  // This is used by lua to send string messages back to the client.
  send_bytes(client, SERVER_LUA_MESSAGE, message, len);
}

void send_set_avatar(Client *client) {
  uint32_t avatar = client->avatar;
  send_bytes(client, SET_AVATAR, (char *)&avatar, 4);
}

void write_snapshot(uv_stream_t *stream, Snapshot *snapshot) {
  uv_write_t *req = (uv_write_t *)malloc(sizeof(uv_write_t));
  
  Header header = {0, SNAPSHOT};
  
  uv_buf_t b[10] = {};
  int used_bufs = 0;
  
  b[used_bufs++] = uv_buf_init((char *)&header, 5); // Eat it, padding.

  uint16_t numUpdates = kv_size(snapshot->updates);
  uint16_t numCreates = kv_size(snapshot->creates);
  uint16_t numRemoves = kv_size(snapshot->removes);
  uint16_t numShipData = kv_size(snapshot->shipdata);
  
  uint8_t flags = (!!numUpdates) | (!!numCreates << 1) | (!!numRemoves << 2) | (!!numShipData << 3)
      | (!!snapshot->radar << 4);
  b[used_bufs++] = uv_buf_init((char *)&flags, 1);
  
  if (numUpdates) {
    b[used_bufs++] = uv_buf_init((char *)&numUpdates, sizeof(numUpdates));
    b[used_bufs++] = uv_buf_init((char *)&kv_A(snapshot->updates, 0), numUpdates * sizeof(UpdateFrame));
  }
  
  if (numCreates) {
    b[used_bufs++] = uv_buf_init((char *)&numCreates, sizeof(numCreates));
    b[used_bufs++] = uv_buf_init((char *)&kv_A(snapshot->creates, 0), numCreates * sizeof(CreateFrame));
  }
  
  if (numRemoves) {
    b[used_bufs++] = uv_buf_init((char *)&numRemoves, sizeof(numRemoves));
    b[used_bufs++] = uv_buf_init((char *)snapshot->removes.a, numRemoves * sizeof(ObjectId));
  }
  
  if (numShipData) {
    b[used_bufs++] = uv_buf_init((char *)&numShipData, sizeof(numShipData));
    b[used_bufs++] = uv_buf_init((char *)&kv_A(snapshot->shipdata, 0), numShipData * sizeof(ShipData));
  }
  
  if (snapshot->radar) {
    uint16_t numHeatSignatures = kv_size(*snapshot->radar);
    b[used_bufs++] = uv_buf_init((char *)&numHeatSignatures, sizeof(numHeatSignatures));
    b[used_bufs++] = uv_buf_init((char *)&kv_A(*snapshot->radar, 0), numHeatSignatures * sizeof(Heat));
  }
  
  assert(used_bufs <= sizeof(b)/sizeof(b[0]));
  
  for (int i = 0; i < used_bufs; i++) {
    header.size += b[i].len;
  }
  header.size -= 4; // Don't include the size of the size field itself.
  
  uv_write(req, stream, b, used_bufs, write_cb);
}



#define MIN(x, y) ((x) < (y) ? (x) : (y))

void read_client_bytes(Client *client, char *dest, int num) {
  int p = 0; // The position in the destination bytes
  while(p != num) {
    uv_buf_t buf = kv_A(client->readBuffers, 0);
    int read = MIN(num - p, (int)buf.len - client->offset);
    memcpy(dest + p, buf.base + client->offset, read);
    p += read;
    client->offset += read;
    if(client->offset == buf.len) {
      client->offset = 0;
      
      free(kv_A(client->readBuffers, 0).base);
      memmove(&kv_A(client->readBuffers, 0), &kv_A(client->readBuffers, 1),
              (kv_size(client->readBuffers) - 1) * sizeof(uv_buf_t));
      kv_size(client->readBuffers)--;
      
      //printf("%zu\n", kv_size(client->readBuffers));
    }
  }
}

/*
 * `nread` is > 0 if there is data available, 0 if libuv is done reading for now
 * or -1 on error.
 *
 * Error details can be obtained by calling uv_last_error(). UV_EOF indicates
 * that the stream has been closed.
 *
 * The callee is responsible for closing the stream when an error happens.
 * Trying to read from the stream again is undefined.
 *
 * The callee is responsible for freeing the buffer, libuv does not reuse it.
 */
void read_cb(uv_stream_t *stream, ssize_t nread, uv_buf_t buf) {
  Client *client = (Client *)stream->data;

  if (nread < 0) {
    free(buf.base);
    uv_err_t err = uv_last_error(uv_default_loop());
    if (err.code == UV_EOF) {
      printf("End of line.\n");
    } else {
      fprintf(stderr, "uv error %s: %s\n", uv_err_name(err), uv_strerror(err));
    }
    client_closed(client);
    uv_close((uv_handle_t *)stream, close_cb);
    return;
  }
  
  if (nread > 0) {
    // Abusing the buffer structure. Maybe I should make a parallel structure and
    // put the bufs in there. Eh.
    buf.len = nread;
    
    kv_push(uv_buf_t, client->readBuffers, buf);
    int pendingBytes = -client->offset;
    for (unsigned int i = 0; i < kv_size(client->readBuffers); i++) {
      pendingBytes += kv_A(client->readBuffers, i).len;
    }
    
    while(true) {
      //printf("pl %d pb %d\n", client->packetLength, pendingBytes);
      if(client->packetLength == -1) {
        if (pendingBytes < 4) {
          break;
        }
        
        // The bytes are already little endian.
        read_client_bytes(client, (char *)&client->packetLength, 4);
        //printf("read header %d\n", client->packetLength);
        pendingBytes -= 4;
        
      } else {
        if (pendingBytes < client->packetLength) {
          break;
        }
        
        // Read out a packet.
        //printf("read packet %d\n", client->packetLength);
        char *bytes = (char *)malloc(client->packetLength + 1); // extra 1 for a '\0'
        bytes[client->packetLength] = '\0';
        read_client_bytes(client, (char *)bytes, client->packetLength);
        // ... and process them.
        
        on_client_data(client, bytes, client->packetLength);
        
        pendingBytes -= client->packetLength;
        client->packetLength = -1;
      }
    }
  }
}

