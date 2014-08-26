//
// Copyright 2013-2014 EvriChart, Inc. All Rights Reserved.
// See LICENSE.txt
//

// C
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctime>

#if defined( __linux__ )
	#include <sys/select.h>
	#include <sys/inotify.h>
#else
	#include <string.h>
	#include <errno.h>

	#include <sys/event.h>
	#include <sys/time.h>
#endif // defined( __linux__ )

// C++
#include <string>
#include <iostream>

// Internal
#include "Eloquent/Logging.h"
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
: IO( i_Config, i_LogMutex, i_Log, i_QueueMutex, i_QueueCV, i_Queue, i_NumWriters )
, m_FilePath( m_Config.second.get<std::string>( "path" ) )
, m_FileStream()
, m_Pos()
{
	m_FileStream.open( m_FilePath.string().c_str(), std::ifstream::binary | std::ifstream::ate );
	m_Pos = m_FileStream.tellg();
	
	std::unique_lock<std::mutex> Lock( m_LogMutex );
	m_Log( Eloquent::LogSeverity::SEV_INFO ) << TimeAndSpace() << "setting up a reader for " << m_FilePath.string() << " #Comment #Filesystem #Reader #FileReader" << std::endl;
	
}

Eloquent::FileReader::~FileReader(){
	std::unique_lock<std::mutex> Lock( m_LogMutex );
	m_Log( Eloquent::LogSeverity::SEV_INFO ) << TimeAndSpace() << "shutting down a reader for " << m_FilePath.string() << " #Comment #Filesystem #Reader #FileReader" << std::endl;
		
}

void Eloquent::FileReader::ReadStream(){
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
		
		PushQueueItem( QueueItem( Buffer.str(), (m_SetOrigin.is_initialized() ? *m_SetOrigin : m_FilePath.string()) ) );
		
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
		m_Log( Eloquent::LogSeverity::SEV_ERROR ) << TimeAndSpace() << e.what() << " #Error #Reader #FileReader" << std::endl;
		
	}
	
}

void Eloquent::FileReader::MonitorINotify() {
#if defined( __linux__ )
	try {
		int fd = inotify_init();
		int dwd = inotify_add_watch( fd, m_FilePath.parent_path().string().c_str(), IN_CREATE );
		int fwd = inotify_add_watch( fd, m_FilePath.string().c_str(), IN_MODIFY | IN_MOVE_SELF );

		bool FileRenamed( false );

		while( true ){
			//boost::this_thread::interruption_point();
			
			char Buffer[4096];
			
			int NumEvents = read( fd, Buffer, sizeof( Buffer ) );
			
			for( int i = 0; i < NumEvents; ++i ) {
				struct inotify_event* Event = (inotify_event*)( &Buffer[i] );
				
				if( Event->mask & IN_MODIFY ) {
					ReadStream();
					
				} else if( Event->mask & IN_MOVE_SELF ) {
					if( !boost::filesystem::exists( m_FilePath.string().c_str() ) ) {
						if( !FileRenamed ) {
							inotify_rm_watch( fd, fwd );
							m_FileStream.close();
							FileRenamed = true;

						}
					}
						
				} else if( Event->mask & IN_CREATE ) {
					if( FileRenamed ) {
						if( std::string( Event->name ) == m_FilePath.filename() ) {
							fwd = inotify_add_watch( fd, m_FilePath.string().c_str(), IN_MODIFY | IN_MOVE_SELF );

							m_FileStream.open( m_FilePath.string().c_str(), std::ifstream::in );
							m_FileStream.seekg( 0, m_FileStream.beg );
							m_Pos = m_FileStream.tellg();

							FileRenamed = false;

						}

					}

				}
				
			}
			
		}

		close( dwd );
		close( fwd );
		close( fd );
		
		if( m_FileStream.is_open() )
			m_FileStream.close();
		
	} catch( const std::exception& e ){
		std::unique_lock<std::mutex> Lock( m_LogMutex );
		m_Log( Eloquent::LogSeverity::SEV_ERROR ) << TimeAndSpace() << e.what() << " #Error #Reader #FileReader" << std::endl;
	} catch( ... ) {
		std::unique_lock<std::mutex> Lock( m_LogMutex );
		m_Log( Eloquent::LogSeverity::SEV_ERROR ) << TimeAndSpace() << "unknown exception #Error #Attention #Reader #FileReader" << std::endl;
	}
	
#endif
	
}

void Eloquent::FileReader::MonitorKQueue() {
#if !defined( __linux__ )
	int NumEvents = 0;
	
	while( true ) {
		try {
			int dd, kq, ev;
			struct kevent ChangeList;
			struct kevent EventList;
			
			kq = kqueue();
			
			bool Renamed = false;

			while( true ) {
				if( boost::filesystem::exists( m_FilePath.string().c_str() ) ) {
					
					if( Renamed ) {
						m_FileStream.open( m_FilePath.string().c_str(), std::ifstream::in );
						
						m_Pos = m_FileStream.tellg();
						
						Renamed = false;
						
					}

					int fd = open( m_FilePath.string().data(), O_RDONLY );

					EV_SET( &ChangeList, fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_ONESHOT, NOTE_EXTEND | NOTE_WRITE | NOTE_RENAME, 0, 0 );
					
					while( true ){
						if( (ev = kevent( kq, &ChangeList, 1, &EventList, 1, NULL )) > 0 ) {
							if( EventList.fflags & NOTE_WRITE || EventList.fflags & NOTE_EXTEND ) {
								ReadStream();
							}
							
							if( EventList.fflags & NOTE_RENAME ) {
								std::unique_lock<std::mutex> Lock( m_LogMutex );
								m_Log( Eloquent::LogSeverity::SEV_INFO ) << TimeAndSpace() << m_FilePath.string() << " renamed. assumed rotation. #Comment #Reader #FileReader" << std::endl;
								
								Renamed = true;
								
								break;
								
							}
							
						} else {
							std::unique_lock<std::mutex> Lock( m_LogMutex );
							m_Log( Eloquent::LogSeverity::SEV_ERROR ) << TimeAndSpace() << std::string( strerror( errno ) ) << " #Error #Reader #FileReader" << std::endl;
							
						}
						
					}
					
					m_FileStream.close();
					close( fd );
					
				}
				
			}
			
			close( kq );
			
			if( m_FileStream.is_open() )
				m_FileStream.close();
			
		} catch( const std::exception& e ){
			std::unique_lock<std::mutex> Lock( m_LogMutex );
			m_Log( Eloquent::LogSeverity::SEV_ERROR ) << TimeAndSpace() << e.what() << " #Error #Reader #FileReader" << std::endl;
			
		} catch( ... ) {
			std::unique_lock<std::mutex> Lock( m_LogMutex );
			m_Log( Eloquent::LogSeverity::SEV_ERROR ) << TimeAndSpace() << "unknown exception #Error #Attention #Reader #FileReader" << std::endl;
			
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
