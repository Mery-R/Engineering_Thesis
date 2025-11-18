template<typename T>
RingBuffer<T>::RingBuffer(int capacity)
  : _cap(capacity), _head(0), _count(0) {
  _buf = new T[_cap];
  _mtx = xSemaphoreCreateMutex();
}

template<typename T>
bool RingBuffer<T>::push(const T &d) {
  bool ok = false;
  if (xSemaphoreTake(_mtx, (TickType_t)10) == pdTRUE) {
    if (_count < _cap) {
      int idx = (_head + _count) % _cap;
      _buf[idx] = d;
      _count++;
      ok = true;
    }
    xSemaphoreGive(_mtx);
  }
  return ok;
}

template<typename T>
int RingBuffer<T>::popBatch(T out[], int maxItems) {
  int got = 0;
  if (xSemaphoreTake(_mtx, portMAX_DELAY) == pdTRUE) {
    int toPop = min(maxItems, _count);
    for (int i = 0; i < toPop; ++i) {
      out[i] = _buf[(_head + i) % _cap];
    }
    _head = (_head + toPop) % _cap;
    _count -= toPop;
    got = toPop;
    xSemaphoreGive(_mtx);
  }
  return got;
}

template<typename T>
int RingBuffer<T>::size() {
  int s = 0;
  if (xSemaphoreTake(_mtx, (TickType_t)10) == pdTRUE) {
    s = _count;
    xSemaphoreGive(_mtx);
  }
  return s;
}

template<typename T>
void RingBuffer<T>::clear() {
  if (xSemaphoreTake(_mtx, portMAX_DELAY) == pdTRUE) {
    _head = 0; _count = 0;
    xSemaphoreGive(_mtx);
  }
}
