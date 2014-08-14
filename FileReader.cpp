//
// Copyright 2013-2014 EvriChart, Inc. All Rights Reserved.
// See LICENSE.txt
//

// C
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#if defined( __linux__ )
	#define DEFAULT_MKFT_LINUX_BUFFER_SIZE 16384

	#include <sys/select.h>
	#include <sys/inotify.h>
#else
	#include <sys/event.h>
	#include <sys/time.h>
#endif // defined( __linux__ )

// C++
#include <string>

// Internal
#include "Eloquent/Global/Logging.h"
#include "FileReader.h"

///////////////////////////////////////////////////////////////////////////////
// FileReader : IOExtension
///////////////////////////////////////////////////////////////////////////////
Eloquent::FileReader::FileReader( const boost::property_tree::ptree::value_type& i_Config
								 , std::mutex& i_LogMutex
								 , streamlog::severity_log& i_Log
								 , std::mutex& i_QueueMutex
								 , std::condition_variable& i_QueueCV
								 , std::queue<QueueItem>& i_Queue
								 , int& i_NumWriters )
: IOExtension( i_Config, i_LogMutex, i_Log, i_QueueMutex, i_QueueCV, i_Queue, i_NumWriters )
, m_FilePath( m_Config.second.get<std::string>( "path" ) )
, m_FileStream()
, m_Pos()
{
	m_FileStream.open( m_FilePath.string().c_str(), std::ifstream::binary | std::ifstream::ate );
	m_Pos = m_FileStream.tellg();
	
	std::unique_lock<std::mutex> Lock( m_LogMutex );
	m_Log( Eloquent::LogSeverity::SEV_INFO ) << "FileReader::FileReader() - info - setting up a reader for " << m_FilePath.string() << std::endl;
	
}

Eloquent::FileReader::~FileReader(){
	std::unique_lock<std::mutex> Lock( m_LogMutex );
	m_Log( Eloquent::LogSeverity::SEV_INFO ) << "FileReader::~FileReader() - info - shutting down a reader for " << m_FilePath.string() << std::endl;
		
}

void Eloquent::FileReader::ReadStream( bool i_Filter ){
	try {
		if( !m_FileStream.is_open() ){
			m_FileStream.open( m_FilePath.string().c_str(), std::ifstream::binary | std::ifstream::ate );
			m_Pos = m_FileStream.tellg();
		}
		
		m_FileStream.seekg( m_Pos );
		
		if( !m_FileStream.good() )
			m_FileStream.seekg( 0, m_FileStream.beg );
		
		std::stringstream Buffer;
		
		while( m_FileStream.good() ) {
			char c = m_FileStream.get();
			
			if( m_FileStream.good() ) {
				Buffer << c;
			}
			
		}
		
		m_FileStream.clear();
		
		m_Pos = m_FileStream.tellg();
		
		std::unique_lock<std::mutex> QueueLock( m_QueueMutex );
		
		if( i_Filter ) {
			m_Queue.push( QueueItem( m_FilterCoordinator->FilterData( Buffer.str() ), (m_SetOrigin.is_initialized() ? *m_SetOrigin : m_FilePath.string()) ) );
		} else {
			m_Queue.push( QueueItem( Buffer.str(), (m_SetOrigin.is_initialized() ? *m_SetOrigin : m_FilePath.string()) ) );
		}
		
		m_QueueCV.notify_one();
		
//		std::cout << "streampos: " << m_FileStream.tellg() << std::endl;
		
//		std::streampos EndPos = m_FileStream.seekg( 0, m_FileStream.end ).tellg();
//		m_FileStream.seekg( m_Pos );
//		
//		char* Buffer = new char[(EndPos - m_Pos)];
//		
//		m_FileStream.read( Buffer, (EndPos - m_Pos) );
//		
//		m_Pos = EndPos;
//		
//		{
//			boost::unique_lock<boost::mutex> QueueLock( m_QueueMutex );
//			
//			if( i_Filter ) {
//				m_Queue.push( QueueItem( m_FilterCoordinator->FilterData( Buffer ) ) );
//			} else {
//				m_Queue.push( QueueItem( Buffer ) );
//			}
//			
//			m_QueueCV.notify_one();
//			
//		}
//		
//		delete[] Buffer;
		
	} catch( const std::exception& e ){
		std::unique_lock<std::mutex> Lock( m_LogMutex );
		m_Log( Eloquent::LogSeverity::SEV_ERROR ) << "FileReader::ReadStream() - error - " << e.what() << std::endl;
		
	}
	
}

void Eloquent::FileReader::MonitorINotify() {
#if defined( __linux__ )
	try {
		int fd = inotify_init();
		int wd = inotify_add_watch( fd, m_FilePath.string().c_str(), IN_MODIFY );
		
		while( true ){
			//boost::this_thread::interruption_point();
			
			if( !m_FileStream.is_open() )
				m_FileStream.open( m_FilePath.string().c_str(), std::ifstream::in | std::ifstream::ate );
			
			char Buffer[DEFAULT_MKFT_LINUX_BUFFER_SIZE] = "";
			
			int NumEvents = read( fd, Buffer, sizeof( Buffer ) );
			
			for( int i = 0; i < NumEvents; ++i ) {
				struct inotify_event* Event = (inotify_event*)( &Buffer[i] );
				
				if( Event->mask & IN_MODIFY ) {
					ReadStream( false );
				}
				
			}
			
		}
		
		close( wd );
		close( fd );
		
		if( m_FileStream.is_open() )
			m_FileStream.close();
		
	} catch( const std::exception& e ){
		std::unique_lock<std::mutex> Lock( m_LogMutex );
		m_Log( Eloquent::LogSeverity::SEV_ERROR ) << "FileReader::ReadStream() - error - std::exception" << std::endl;
	} catch( ... ) {
		std::unique_lock<std::mutex> Lock( m_LogMutex );
		m_Log( Eloquent::LogSeverity::SEV_ERROR ) << "FileReader::ReadStream() - error - unknown exception" << std::endl;
	}
	
#endif
	
}

void Eloquent::FileReader::MonitorKQueue() {
#if !defined( __linux__ )
	boost::optional<std::string> Filter = m_Config.second.get_optional<std::string>( "filter" );
	
	int NumEvents = 0;
	
	while( true ) {
		try {
			int fd, kq, ev;
			
			kq = kqueue();
			fd = open( m_FilePath.string().data(), O_RDONLY );
			
			struct kevent ChangeList;
			struct kevent EventList;
			
			EV_SET( &ChangeList, fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_ONESHOT, NOTE_DELETE | NOTE_EXTEND | NOTE_WRITE | NOTE_ATTRIB, 0, 0 );
			
			while( true ){
				ev = kevent( kq, &ChangeList, 1, &EventList, 1, NULL );
				
				if( ev == -1 ) {
					std::unique_lock<std::mutex> Lock( m_LogMutex );
					m_Log( Eloquent::LogSeverity::SEV_ERROR ) << "FileReader::MonitorKQueue() - error - kqueue error" << std::endl;
					
				} else if( ev > 0 ) {
					if( EventList.fflags & NOTE_WRITE || EventList.fflags & NOTE_EXTEND ) {
						ReadStream( Filter.is_initialized() );
					}
					
				}
				
			}
			
			close( kq );
			close( fd );
			
			if( m_FileStream.is_open() )
				m_FileStream.close();
			
		} catch( const std::exception& e ){
			std::unique_lock<std::mutex> Lock( m_LogMutex );
			m_Log( Eloquent::LogSeverity::SEV_ERROR ) << "FileReader::ReadStream() - error - std::exception" << std::endl;
			
		} catch( ... ) {
			std::unique_lock<std::mutex> Lock( m_LogMutex );
			m_Log( Eloquent::LogSeverity::SEV_ERROR ) << "FileReader::MonitorKQueue() - error - unknown exception" << std::endl;
			
		}
		
	}
	
#endif
	
}

void Eloquent::FileReader::operator()(){
#if defined( __linux__ )
	MonitorINotify();
#else
	MonitorKQueue();
#endif
	
	delete this;
	
}	