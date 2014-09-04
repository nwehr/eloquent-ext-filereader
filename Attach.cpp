#include "MyReader.h"
#include "MyReaderFactory.h"

extern "C" void* Attach(void) {
	return new Eloquent::FileReaderFactory();
}
