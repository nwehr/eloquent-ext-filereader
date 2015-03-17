#ifndef _FileReader_h
#define _FileReader_h

//
// Copyright 2013-2014 EvriChart, Inc. All Rights Reserved.
// See LICENSE.txt
//

// C
#include <syslog.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctime>

#ifdef __linux__
#include <sys/select.h>
#include <sys/inotify.h>
#include <limits.h>

#else
#include <string.h>
#include <errno.h>
#include <sys/event.h>
#include <sys/time.h>

#endif // defined( __linux__ )

// C++
#include <fstream>
#include <iostream>

// Boost
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>

// Internal
#include "Eloquent/Extensions/IO/IO.h"

namespace Eloquent {
	///////////////////////////////////////////////////////////////////////////////
	// FileReader : IO
	///////////////////////////////////////////////////////////////////////////////
	class FileReader : public IO {
	public:
		FileReader() = delete;
		
		explicit FileReader( const boost::property_tree::ptree::value_type& i_Config
							, std::mutex& i_QueueMutex
							, std::condition_variable& i_QueueCV
							, std::queue<QueueItem>& i_Queue
							, unsigned int& i_NumWriters );

		virtual ~FileReader();
		
		void ReadStream();
		
		virtual void operator()();

	private:
		// File Location/Data
		boost::filesystem::path m_FilePath;
		std::ifstream 			m_FileStream;
		
		std::streampos			m_Pos;
		
		bool m_RestartFileStream;
		bool m_RestartEventLoop;
	};

}

#endif // _FileReader_h
