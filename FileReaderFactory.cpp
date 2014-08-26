//
// Copyright 2013-2014 EvriChart, Inc. All Rights Reserved.
// See LICENSE.txt
//

#include "FileReaderFactory.h"
#include "Eloquent/Logging.h"

///////////////////////////////////////////////////////////////////////////////
// FileReaderFactory : IOExtensionFactory
///////////////////////////////////////////////////////////////////////////////
Eloquent::FileReaderFactory::FileReaderFactory() {}
Eloquent::FileReaderFactory::~FileReaderFactory() {}

Eloquent::IO* Eloquent::FileReaderFactory::New( const boost::property_tree::ptree::value_type& i_Config
														, std::mutex& i_LogMutex
														, streamlog::severity_log& i_Log
														, std::mutex& i_QueueMutex
														, std::condition_variable& i_QueueCV
														, std::queue<QueueItem>& i_Queue
														, int& i_NumWriters )
{
	{
		std::unique_lock<std::mutex> LogLock( i_LogMutex );
		i_Log( Eloquent::LogSeverity::SEV_DEBUG ) << TimeAndSpace() << "returning new reader #Comment #Factory #FileReaderFactory" << std::endl;
	}
	
	return new FileReader( i_Config, i_LogMutex, i_Log, i_QueueMutex, i_QueueCV, i_Queue, i_NumWriters );
}
