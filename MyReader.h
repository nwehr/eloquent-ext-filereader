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
		, m_FileMoved( false )
		{
			m_FileStream.open( m_FilePath.string().c_str(), std::ifstream::binary | std::ifstream::ate );
			m_Pos = m_FileStream.tellg();
		}

		virtual ~FileReader() {}
		
		void ReadStream() {
			try {
				// Reopen the file stream if necessary
				if( !m_FileStream.is_open() ) {
					std::ifstream::openmode Mode = std::ifstream::binary;
					
					if( m_FileMoved ) {
						// Set the open mode to the beginning if the file was rotated...
						Mode | std::ifstream::beg;
						
						// Reset the FileMoved sentinal...
						m_FileMoved = false;
						
					} else {
						// Set the open mode to the end of the file...
						Mode | std::ifstream::ate;
					}
					
					m_FileStream.open( m_FilePath.string().c_str(), Mode );
					
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
					PushQueueItem( QueueItem( Data, (m_SetOrigin.is_initialized() ? *m_SetOrigin : m_FilePath.string()) ) );
					
					// Empty the data string so we can write to it again if needed
					Data.clear();
				}
				
			} catch( const std::exception& e ){
				syslog( LOG_ERR, "%s #Error #Attention #Reader #FileReader", e.what() );
			}
			
		}
		
		virtual void operator()() {
			try {
#ifdef __linux__
				while( true ) {
					// See if our file exists. If not, wait for it to exist (newly rotated logs aren't necessarily recreated right away)
					while( !boost::filesystem::exists( m_FilePath.string().data() ) ) {
						syslog( LOG_ERR, "%s does not exist #Error #FileReader::operator()()", m_FilePath.string().data() );
						sleep( 60 );
					}
					
					// File descriptor for inotify
					int fd = inotify_init();
					
					if( fd == -1 ) {
						syslog( LOG_ERR, "unable to setup file descriptor inotify #Error #FileReader::operator()()" );
					}
					
					// Watch descriptor for an item we want to get events from
					int wd = inotify_add_watch( fd, m_FilePath.string().data(), IN_ALL_EVENTS );
					
					if( wd == -1 ) {
						syslog( LOG_ERR, "unable to setup watch descriptor inotify #Error #FileReader::operator()()" );
					}
					
					// Set up a buffer to put events into
					const unsigned int BUFFER_LEN	= (10* sizeof( struct inotify_event) + NAME_MAX + 1);
					char BUFFER[BUFFER_LEN]			= { "" };
					
					bool ContinueWhile = true;
					
					while( ContinueWhile ) {
						ssize_t BytesRead = read( fd, BUFFER, BUFFER_LEN );
						
						// If we run into an error, break out of this loop and set everything up again
						if( BytesRead == -1 ) {
							syslog( LOG_ERR, "unable to read inotify event #Error #FileReader::operator()()" );
							break;
						}
						
						// Loop through our events and determine if the file was modified or moved
						for( char* p = BUFFER; p < BUFFER + BytesRead; ) {
							struct inotify_event* Event = reinterpret_cast<struct inotify_event*>( p );
							
							if( Event->mask & IN_MOVE_SELF ) {
								// Break out of the while loop
								ContinueWhile	= false;
								
								// Set the FileMoved sentinal
								m_FileMoved		= true;
								
								// Break out of the for loop
								break;
								
							}
							
							if( Event->mask & IN_MODIFY ) {
								// Read data
								ReadStream();
							}
							
							p += sizeof(struct inotify_event) + Event->len;
							
						}
						
					}
					
					// Close file stream
					if( m_FileStream.is_open() )
						m_FileStream.close();
					
				}
#else
				while( true ) {
					// See if our file exists. If not, wait for it to exist (newly rotated logs aren't necessarily recreated right away)
					while( !boost::filesystem::exists( m_FilePath.string().data() ) ) {
						syslog( LOG_ERR, "%s does not exist #Error #FileReader::operator()()", m_FilePath.string().data() );
						sleep( 60 );
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
						
						if( EvList->fflags & NOTE_RENAME ) {
							// Set the FileMoved sentinal
							m_FileMoved = true;
							
							// Break out of the while loop
							break;
							
						}
						
						if( EvList->fflags & NOTE_WRITE || EvList->fflags & NOTE_EXTEND ) {
							ReadStream();
						}
						
					}
					
					close( fd );
					close( kq );
					
					if( m_FileStream.is_open() )
						m_FileStream.close();
					
				}
#endif
				
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
		
		bool m_FileMoved;
		
	};

}

#endif // _FileReader_h
