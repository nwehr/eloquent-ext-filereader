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

//#if defined( __linux__ )
//	#include <sys/select.h>
//	#include <sys/inotify.h>
//#else
	#include <string.h>
	#include <errno.h>

	#include <sys/event.h>
	#include <sys/time.h>
//#endif // defined( __linux__ )

// C++
#include <fstream>

// Boost
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>

// Internal
#include "Eloquent/Extensions/IO/IO.h"

namespace Eloquent {
	///////////////////////////////////////////////////////////////////////////////
	// FileReader : IOExtension
	///////////////////////////////////////////////////////////////////////////////
	class FileReader : public IO {
	public:
		FileReader() = delete;
		
		explicit FileReader( const boost::property_tree::ptree::value_type& i_Config
							, std::mutex& i_QueueMutex
							, std::condition_variable& i_QueueCV
							, std::queue<QueueItem>& i_Queue
							, unsigned int& i_NumWriters )
		: IO( i_Config, i_QueueMutex, i_QueueCV, i_Queue, i_NumWriters )
		, m_FilePath( m_Config.second.get<std::string>( "path" ) )
		, m_FileStream()
		, m_Pos()
		{
			m_FileStream.open( m_FilePath.string().c_str(), std::ifstream::binary | std::ifstream::ate );
			m_Pos = m_FileStream.tellg();
			
			syslog( LOG_INFO, "setting up a reader for %s #Comment #Filesystem #Reader #FileReader", m_FilePath.string().c_str() );
			
		}

		virtual ~FileReader() {
			syslog( LOG_INFO, "shutting down a reader for %s #Comment #Filesystem #Reader #FileReader", m_FilePath.string().c_str() );
		}
		
		void ReadStream() {
			try {
				// Reopen the file stream if necessary and move m_Pos to the end of the file
				if( !m_FileStream.is_open() ){
					m_FileStream.open( m_FilePath.string().c_str(), std::ifstream::binary | std::ifstream::ate );
					m_Pos = m_FileStream.tellg();
				}
				
				// Seek to the last recorded file position
				m_FileStream.seekg( m_Pos );
				
				// TODO: not sure if this is needed anymore... also not sure if its even correct...
				if( !m_FileStream.good() )
					m_FileStream.seekg( 0, m_FileStream.beg );
				
				std::stringstream Buffer;
				
				while( m_FileStream.good() ) {
					char c = m_FileStream.get();
					
					if( m_FileStream.good() ) {
						Buffer << c;
					}
					
				}
				
				// This removes any eof or bad bits that were set by reaching the end of the file
				m_FileStream.clear();
				
				// Record our current position for use later
				m_Pos = m_FileStream.tellg();
				
				std::string Data;
				
				// Break buffered data up into lines. Huge queue items can cause network-based writers
				// (eg DatagramWriter) to not be able to write any data.
				while( std::getline( Buffer, Data ) ) {
					// getline removes the delimiter (in this case "\n"), we need to put it back
					// Data.append( "\n" );
					
					PushQueueItem( QueueItem( Data, (m_SetOrigin.is_initialized() ? *m_SetOrigin : m_FilePath.string()) ) );
					
					Data.clear();
					
				}
				
			} catch( const std::exception& e ){
				syslog( LOG_ERR, "%s #Error #Attention #Reader #FileReader", e.what() );
			}
			
		}
		
//		void MonitorINotify() {
//#if defined( __linux__ )
//			try {
//				int fd = inotify_init();
//				int dwd = inotify_add_watch( fd, m_FilePath.parent_path().string().c_str(), IN_CREATE );
//				int fwd = inotify_add_watch( fd, m_FilePath.string().c_str(), IN_MODIFY | IN_MOVE_SELF );
//				
//				bool FileRenamed( false );
//				
//				while( true ){
//					char Buffer[4096];
//					
//					int NumEvents = read( fd, Buffer, sizeof( Buffer ) );
//					
//					for( int i = 0; i < NumEvents; ++i ) {
//						struct inotify_event* Event = (inotify_event*)( &Buffer[i] );
//						
//						if( Event->mask & IN_MODIFY ) {
//							ReadStream();
//							
//						} else if( Event->mask & IN_MOVE_SELF ) {
//							if( !boost::filesystem::exists( m_FilePath.string().c_str() ) ) {
//								if( !FileRenamed ) {
//									inotify_rm_watch( fd, fwd );
//									m_FileStream.close();
//									FileRenamed = true;
//									
//								}
//								
//							}
//							
//						} else if( Event->mask & IN_CREATE ) {
//							if( FileRenamed ) {
//								if( std::string( Event->name ) == m_FilePath.filename() ) {
//									fwd = inotify_add_watch( fd, m_FilePath.string().c_str(), IN_MODIFY | IN_MOVE_SELF );
//									
//									m_FileStream.open( m_FilePath.string().c_str(), std::ifstream::in );
//									m_FileStream.seekg( 0, m_FileStream.beg );
//									m_Pos = m_FileStream.tellg();
//									
//									FileRenamed = false;
//									
//								}
//								
//							}
//							
//						}
//						
//					}
//					
//				}
//				
//				close( dwd );
//				close( fwd );
//				close( fd );
//				
//				if( m_FileStream.is_open() )
//					m_FileStream.close();
//				
//			} catch( const std::exception& e ){
//				syslog( LOG_ERR, "%s #Error #Attention #Reader #FileReader", e.what() );
//			} catch( ... ) {
//				syslog( LOG_ERR, "unknown exception #Error #Attention #Reader #FileReader" );
//			}
//			
//#endif
//
//		}
		
		void MonitorKQueue() {
			while( true ) {
				try {
					int kq
					, ev;
					
					struct kevent ChangeList
					, EventList;
					
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
										syslog( LOG_INFO, "%s renamed, assumed rotation #Comment #Reader #FileReader", m_FilePath.string().c_str() );
										Renamed = true;
										break;
										
									}
									
								} else {
									syslog( LOG_ERR, "%s #Error #Reader #FileReader", strerror( errno ) );
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
					syslog( LOG_ERR, "%s #Error #Reader #FileReader", e.what() );
					
				} catch( ... ) {
					syslog( LOG_ERR, "unknown exception #Error #Attention #Reader #FileReader" );
				}
				
			}
			
		}
		
		virtual void operator()() {
			MonitorKQueue();
		}

	private:
		// File Location/Data
		boost::filesystem::path m_FilePath;
		std::ifstream 			m_FileStream;
		
		std::streampos			m_Pos;
		
	};

}

#endif // _FileReader_h
