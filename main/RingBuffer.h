#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

template<typename T>
class RingBuffer {
public:
  RingBuffer(int capacity = 10);
  ~RingBuffer();
  bool push(const T &d);         
  int popBatch(T out[], int maxItems); 
  int size();
  void clear();
private:
  T* _buf;
  int _cap;
  int _head;
  int _count;
  SemaphoreHandle_t _mtx;
};

#include "RingBuffer.tpp"
#endif
