#ifndef __eloquent__FileReaderFactory__
#define __eloquent__FileReaderFactory__

//
// Copyright 2013-2014 EvriChart, Inc. All Rights Reserved.
// See LICENSE.txt
//

// C++
#include <vector>
#include <string>

// Boost
#include <boost/property_tree/ptree.hpp>

// Internal
#include "FileReader.h"
#include "Eloquent/Extensions/Factories/IOExtensionFactory.h"

namespace Eloquent {
	///////////////////////////////////////////////////////////////////////////////
	// FileReaderFactory : IOExtensionFactory
	///////////////////////////////////////////////////////////////////////////////
	class FileReaderFactory : public IOExtensionFactory {
	public:
		FileReaderFactory();
		virtual ~FileReaderFactory();
		
		virtual IOExtension* New( const boost::property_tree::ptree::value_type& i_Config
								 , std::mutex& i_LogMutex
								 , streamlog::severity_log& i_Log
								 , std::mutex& i_QueueMutex
								 , std::condition_variable& i_QueueCV
								 , std::queue<QueueItem>& i_Queue
								 , int& i_NumWriters );
		
	};
}

#endif /* defined(__eloquent__FileReaderFactory__) */
