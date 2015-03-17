//
// Copyright 2013-2014 EvriChart, Inc. All Rights Reserved.
// See LICENSE.txt
//

#include "FileReader.h"

///////////////////////////////////////////////////////////////////////////////
// FileReader : IO
///////////////////////////////////////////////////////////////////////////////
Eloquent::FileReader::FileReader( const boost::property_tree::ptree::value_type& i_Config
					, std::mutex& i_QueueMutex
					, std::condition_variable& i_QueueCV
					, std::queue<QueueItem>& i_Queue
					, unsigned int& i_NumWriters )
: IO( i_Config, i_QueueMutex, i_QueueCV, i_Queue, i_NumWriters )
, m_FilePath( m_Config.second.get<std::string>( "path" ) )
, m_FileStream()
, m_Pos()
, m_RestartFileStream(false)
, m_RestartEventLoop(false)
{
	m_FileStream.open( m_FilePath.string().c_str(), std::ifstream::binary | std::ifstream::ate );
	m_Pos = m_FileStream.tellg();
	m_FileStream.close();
	
	syslog(LOG_DEBUG, "starting up reader #Debug #Reader #FileReader");
}

Eloquent::FileReader::~FileReader() {
	syslog(LOG_DEBUG, "shutting down reader #Debug #Reader #FileReader");
}
	
void Eloquent::FileReader::ReadStream()
{
	try {
		m_FileStream.open( m_FilePath.string().c_str(), std::ifstream::binary );
		
		// syslog(LOG_DEBUG, "opened file #Debug #Reader #FileReader");
		
		// reposition to the beginning
		if( m_RestartFileStream ) {
			m_RestartFileStream = false;
			
			m_FileStream.seekg( 0, m_FileStream.beg );
			m_Pos = m_FileStream.tellg();
			
			syslog(LOG_DEBUG, "reading from beginning #Debug #Reader #FileReader");
		} else if( m_Pos ) {
			m_FileStream.seekg( m_Pos );
			
			// see if file has rotated
			if( !m_FileStream.good() ) {
				m_RestartFileStream = true;
				m_RestartEventLoop = true;
				
				syslog(LOG_DEBUG, "file rotated #Debug #Reader #FileReader");
				
				// kind of hacked up...
				ReadStream();
				
				return;
			}
			
		} else {
			m_FileStream.seekg( 0, m_FileStream.end );
			m_Pos = m_FileStream.tellg();
			
			// syslog(LOG_DEBUG, "reading from end #Debug #Reader #FileReader");
		}
		
		// TODO: not sure if this is needed anymore... also not sure if its even correct...
		if( !m_FileStream.good() ) {
			syslog(LOG_DEBUG, "file not longer good, seeking to the beginning #Debug #Reader #FileReader");
			m_FileStream.seekg( 0, m_FileStream.beg );
		}
		
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
			
			// syslog(LOG_DEBUG, "pushed item to core #Debug #Reader #FileReader");
			
			// Empty the data string so we can write to it again if needed
			Data.clear();
		}
		
		m_FileStream.close();
		
		// syslog(LOG_DEBUG, "closed file #Debug #Reader #FileReader");
		
	} catch( const std::exception& e ){
		syslog(LOG_ERR, "%s #Error #Attention #Reader #FileReader", e.what());
	}
	
}

void Eloquent::FileReader::operator()()
{
	try {
#ifdef __linux__
		while( true ) {
			if( m_RestartEventLoop ) {
				m_RestartEventLoop = false;
			}
			
			// See if our file exists. If not, wait for it to exist (newly rotated logs aren't necessarily recreated right away)
			for( unsigned int Wait = 60; !boost::filesystem::exists( m_FilePath.string().data() ) && Wait < 7680; Wait *= 2 ) {
				syslog( LOG_ERR, "%s does not exist; checking again in %d minute(s) #Error #FileReader::operator()()", m_FilePath.string().data(), (Wait / 60) );
				sleep( Wait );
			}
			
			// File descriptor for inotify
			int fd = inotify_init();
			
			if( fd == -1 ) {
				syslog( LOG_ERR, "unable to setup file descriptor for inotify #Error #FileReader::operator()()" );
			}
			
			// Watch descriptor for an item we want to get events from
			int wd = inotify_add_watch( fd, m_FilePath.string().data(), IN_ALL_EVENTS );
			
			if( wd == -1 ) {
				syslog( LOG_ERR, "unable to setup watch descriptor for inotify #Error #FileReader::operator()()" );
			}
			
			// Set up a buffer to put events into
			const unsigned int BUFFER_LEN	= (10* sizeof(struct inotify_event) + NAME_MAX + 1);
			char BUFFER[BUFFER_LEN]			= { "" };
			
			while( !m_RestartEventLoop ) {
				ssize_t BytesRead = read( fd, BUFFER, BUFFER_LEN );
				
				// If we run into an error, break out of this loop and set everything up again
				if( BytesRead == -1 ) {
					syslog( LOG_ERR, "unable to read inotify event for %s #Error #Reader #FileReader", m_FilePath.string().c_str() );
					break;
				}
				
				// Loop through our events and determine if the file was modified or moved
				for( char* p = BUFFER; p < BUFFER + BytesRead; ) {
					struct inotify_event* Event = reinterpret_cast<struct inotify_event*>( p );
					
					if( Event->mask & IN_MOVE_SELF ) {
						syslog(LOG_DEBUG, "renamed/moved %s #Debug #Reader #FileReader", m_FilePath.string().c_str());
						
						m_RestartEventLoop = true;
						m_RestartFileStream = true;
						
						// Break out of the for loop
						break;
						
					}
					
					if( Event->mask & IN_MODIFY ) {
						// syslog(LOG_DEBUG, "modified %s #Debug #Reader #FileReader", m_FilePath.string().c_str());
						
						// Read data
						ReadStream();
					}
					
					p += sizeof(struct inotify_event) + Event->len;
				}
			}
			
			// syslog(LOG_DEBUG, "closed %s #Info #Reader #FileReader", m_FilePath.string().data());
			
		}
#else
		while( true ) {
			if( m_RestartEventLoop ) {
				m_RestartEventLoop = false;
			}
			
			// See if our file exists. If not, wait for it to exist (newly rotated logs aren't necessarily recreated right away)
			// Wait time is doubled from 1 minute to 128 minutes
			for( unsigned int Wait = 60; !boost::filesystem::exists( m_FilePath.string().data() ) && Wait < 7680; Wait *= 2 ) {
				syslog( LOG_ERR, "%s does not exist; checking again in %d minute(s) #Error #FileReader", m_FilePath.string().data(), (Wait / 60) );
				sleep( Wait );
			}
			
			// Create a file descriptor for kqueue
			int fd = open( m_FilePath.string().data(), O_RDONLY );
			int kq = kqueue();
			
			if( !fd ) {
				syslog(LOG_DEBUG, "unable to read %s for events #Info #Reader #FileReader", m_FilePath.string().data());
				continue;
			}
		
			// The events we're going to get
			int NumEvents = 1;
			struct kevent EvList[NumEvents];
			
			
			// The events that we're looking for
			int NumChanges = 1;
			struct kevent ChList[NumChanges];
			
			// Configure our event tracking
			{
				int Filter	= EVFILT_VNODE;
				int Flags	= EV_ADD | EV_CLEAR;
				int FFlags	= NOTE_WRITE | NOTE_DELETE;
				long Data	= 0;
				void* UData	= 0;
				
				// Populate ChList with the correct values
				EV_SET( ChList, fd, Filter, Flags, FFlags, Data, UData );
			}
			
			while( !m_RestartEventLoop ) {
				// Wait for new events
				int NumOccured = kevent( kq, ChList, NumChanges, EvList, NumEvents, NULL );
				
				if( NumOccured == -1 ) {
					syslog(LOG_ERR, "kqueue error for %s #Error #Reader #FileReader", m_FilePath.string().c_str());
					
				} else if( NumOccured > 0 ) {
					// syslog(LOG_DEBUG, "%d kqueue event(s) for %s #Debug #Reader #FileReader", NumOccured, m_FilePath.string().c_str());
					
					for( int i = 0; i < NumOccured; ++i ) {
						if( EvList[i].fflags & NOTE_DELETE ) {
							syslog(LOG_DEBUG, "deleted %s #Debug #Reader #NOTE_DELETE #FileReader", m_FilePath.string().c_str());
							
							m_RestartEventLoop = true;
							m_RestartFileStream = true;
							
							break; 
						}
						
						if( EvList[i].fflags & NOTE_WRITE ) {
							// syslog(LOG_DEBUG, "wrote to %s #Debug #Reader #NOTE_WRITE #FileReader", m_FilePath.string().c_str());
							ReadStream();
						}
					}
				}
			}
			
			close( fd );
			close( kq );
			
			// syslog(LOG_DEBUG, "closed %s #Info #Reader #FileReader", m_FilePath.string().data());
		}
#endif
		
	} catch( const std::exception& e ){
		syslog( LOG_ERR, "%s #Error #Reader #FileReader", e.what() );
	} catch( ... ) {
		syslog( LOG_ERR, "unknown exception #Error #Attention #Reader #FileReader" );
	}
	
}
