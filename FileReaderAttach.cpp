//
// Copyright 2013-2014 EvriChart, Inc. All Rights Reserved.
// See LICENSE.txt
//

// Internal
#include "FileReaderAttach.h"

namespace Eloquent {
	static const std::string EXTENSION_NAME		= "file";
	static const std::string EXTENSION_VERSION	= "1.0";
	static const std::string EXTENSION_AUTHOR	= "Nathan Wehr <nathanw@evrichart.com>";
	static const std::string EXTENSION_TYPE		= "read";
	
	static std::vector<std::string> EXTENSION_KEYS;

}


///////////////////////////////////////////////////////////////////////////////
// Library Initializer
///////////////////////////////////////////////////////////////////////////////
extern "C" void* Attach( void ) {
	Eloquent::EXTENSION_KEYS.push_back( "location" );
	Eloquent::EXTENSION_KEYS.push_back( "format" );
	
	return new Eloquent::FileReaderFactory( Eloquent::EXTENSION_NAME, Eloquent::EXTENSION_VERSION, Eloquent::EXTENSION_AUTHOR, Eloquent::EXTENSION_TYPE, Eloquent::EXTENSION_KEYS );
	
}
