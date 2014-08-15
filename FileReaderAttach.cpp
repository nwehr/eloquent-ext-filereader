//
// Copyright 2013-2014 EvriChart, Inc. All Rights Reserved.
// See LICENSE.txt
//

// Internal
#include "Eloquent/Attach.h"
#include "FileReaderFactory.h"

///////////////////////////////////////////////////////////////////////////////
// Library Initializer
///////////////////////////////////////////////////////////////////////////////
extern "C" void* Attach( void ) {
	return new Eloquent::FileReaderFactory();
}
