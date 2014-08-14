//
// Copyright 2013-2014 EvriChart, Inc. All Rights Reserved.
// See LICENSE.txt
//

#include "FileReader.h"
#include "FileReaderFactory.h"

///////////////////////////////////////////////////////////////////////////////
// FileReaderFactory : IOExtensionFactory
///////////////////////////////////////////////////////////////////////////////
Eloquent::FileReaderFactory::FileReaderFactory( const std::string& i_Name
											   , const std::string& i_Version
											   , const std::string& i_Author
											   , const std::string& i_Type
											   , const std::vector<std::string>& i_Keys )
: IOExtensionFactory( i_Name, i_Version, i_Author, i_Type, i_Keys )
{}

Eloquent::FileReaderFactory::~FileReaderFactory(){}

Eloquent::IOExtension* Eloquent::FileReaderFactory::New( const boost::property_tree::ptree::value_type& i_Config
														, std::mutex& i_LogMutex
														, streamlog::severity_log& i_Log
														, std::mutex& i_QueueMutex
														, std::condition_variable& i_QueueCV
														, std::queue<QueueItem>& i_Queue
														, int& i_NumWriters )
{
	return new FileReader( i_Config, i_LogMutex, i_Log, i_QueueMutex, i_QueueCV, i_Queue, i_NumWriters );
}
