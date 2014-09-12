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
#include <iostream>

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
		
		virtual void operator()() {
			try {
				while( true ) {
					// No point in tailing a file that doesn't exist...
					if( !boost::filesystem::exists( m_FilePath.string().c_str() ) ) {
						throw std::runtime_error( "file does not exist" );
					}
					
					// Create a file descriptor for kqueue
					int fd = open( m_FilePath.string().data(), O_RDONLY );
					int kq = kqueue();
					
					while( true ){
						// The events we're going to get
						int NumEvents = 1;
						struct kevent EvList[NumEvents];
						
						
						// The events that we're looking for
						int NumChanges = 1;
						struct kevent ChList[NumChanges];
						
						// Configure our event tracking
						{
							int Filter	= EVFILT_VNODE;
							int Flags	= EV_ADD | EV_ENABLE | EV_ONESHOT;
							int FFlags	= NOTE_WRITE | NOTE_EXTEND | NOTE_RENAME;
							long Data	= 0;
							void* UData	= 0;
							
							// Populate ChList with the correct values
							EV_SET( ChList, fd, Filter, Flags, FFlags, Data, UData );
							
						}
						
						// Wait for new events
						int NumOccured = kevent( kq, ChList, NumChanges, EvList, NumEvents, NULL );
						
						// Zero events occured
						if( NumOccured < 1 ) {
							if( EvList->flags & EV_ERROR || NumOccured == -1 ) {
								std::cout << strerror( errno ) << std::endl;
								continue;
							}
							
						}
						
						if( EvList->fflags & NOTE_WRITE || EvList->fflags & NOTE_EXTEND ) {
							std::cout << "Extended" << std::endl;
							ReadStream();
						}
						
						if( EvList->fflags & NOTE_RENAME ) {
							std::cout << "Renamed" << std::endl;
							break;
						}
						
					}
					
					close( fd );
					close( kq );
					
					if( m_FileStream.is_open() )
						m_FileStream.close();
					
					sleep( 3 );
					
				}
				
			} catch( const std::exception& e ){
				syslog( LOG_ERR, "%s #Error #Reader #FileReader", e.what() );
				
			} catch( ... ) {
				syslog( LOG_ERR, "unknown exception #Error #Attention #Reader #FileReader" );
			}
			
		}

	private:
		// File Location/Data
		boost::filesystem::path m_FilePath;
		std::ifstream 			m_FileStream;
		
		std::streampos			m_Pos;
		
	};

}

#endif // _FileReader_h
