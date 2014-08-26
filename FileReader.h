#ifndef _FileReader_h
#define _FileReader_h

//
// Copyright 2013-2014 EvriChart, Inc. All Rights Reserved.
// See LICENSE.txt
//

// C++
#include <fstream>

// Boost
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>

// External
#include <streamlog/streamlog.h>

// Internal
#include "Eloquent/Extensions/IO/IO.h"

namespace Eloquent {
	///////////////////////////////////////////////////////////////////////////////
	// FileReader : IOExtension
	///////////////////////////////////////////////////////////////////////////////
	class FileReader : public IO {
		FileReader();
	public:
		explicit FileReader( const boost::property_tree::ptree::value_type& i_Config
							, std::mutex& i_LogMutex
							, streamlog::severity_log& i_Log
							, std::mutex& i_QueueMutex
							, std::condition_variable& i_QueueCV
							, std::queue<QueueItem>& i_Queue
							, int& i_NumWriters );

		virtual ~FileReader();
		
		void ReadStream();
		
		void MonitorINotify();
		void MonitorKQueue();
		
		virtual void operator()();

	private:
		// File Location/Data
		boost::filesystem::path m_FilePath;
		std::ifstream 			m_FileStream;
		
		std::streampos			m_Pos;
		
	};

}

#endif // _FileReader_h
