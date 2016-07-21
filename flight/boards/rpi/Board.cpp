/*
 * BCFlight
 * Copyright (C) 2016 Adrien Aubry (drich)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/

#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/signal.h>

extern "C" {
#include <interface/vmcs_host/vc_vchi_gencmd.h>
#include <interface/vmcs_host/vc_vchi_bufman.h>
#include <interface/vmcs_host/vc_tvservice.h>
#include <interface/vmcs_host/vc_cecservice.h>
#include <interface/vchiq_arm/vchiq_if.h>
};

#include <wiringPi.h>
#include <fstream>
#include <Main.h>
#include "Board.h"
#include "I2C.h"

extern "C" void bcm_host_init( void );
extern "C" void bcm_host_deinit( void );

uint64_t Board::mTicksBase = 0;
uint64_t Board::mLastWorkJiffies = 0;
uint64_t Board::mLastTotalJiffies = 0;
bool Board::mUpdating = false;
decltype(Board::mRegisters) Board::mRegisters = decltype(Board::mRegisters)();
PWM* Board::mMotorsPWM = nullptr;

VCHI_INSTANCE_T Board::global_initialise_instance = nullptr;
VCHI_CONNECTION_T* Board::global_connection = nullptr;


Board::Board( Main* main )
{
	bcm_host_init();
// 	VCOSInit();

	wiringPiSetupGpio();

	//TODO : load later, using pins specified in config file
// 	uint8_t motors_pins[] = { 18, 23, 24, 25 };
// 	mMotorsPWM = new PWM( 14, 1000000, 2000, 2, motors_pins, 4 );

	system( "mount -o remount,rw /data" );

	atexit( &Board::AtExit );

	std::ifstream file( "/data/flight_regs" );
	std::string line;
	if ( file.is_open() ) {
		while ( std::getline( file, line, '\n' ) ) {
			std::string key = line.substr( 0, line.find( "=" ) );
			std::string value = line.substr( line.find( "=" ) + 1 );
			mRegisters[ key ] = value;
		}
		file.close();
	}
}


Board::~Board()
{
}


void Board::AtExit()
{
}


PWM* Board::motorsPWM()
{
	return mMotorsPWM;
}


void Board::UpdateFirmwareData( const uint8_t* buf, uint32_t offset, uint32_t size )
{
	gDebug() << "Saving Flight Controller firmware update (" << size << " bytes at offset " << offset << ")\n";

	if ( offset == 0 ) {
		system( "rm -f /tmp/flight_update" );
		system( "touch /tmp/flight_update" );
	}

	std::fstream firmware( "/tmp/flight_update", std::ios_base::in | std::ios_base::out | std::ios_base::binary );
	firmware.seekg( offset, firmware.beg );
	firmware.write( (char*)buf, size );
	firmware.close();

}


static uint32_t crc32( const uint8_t* buf, uint32_t len )
{
	uint32_t k = 0;
	uint32_t crc = 0;

	crc = ~crc;

	while ( len-- ) {
		crc ^= *buf++;
		for ( k = 0; k < 8; k++ ) {
			crc = ( crc & 1 ) ? ( (crc >> 1) ^ 0x82f63b78 ) : ( crc >> 1 );
		}
	}

	return ~crc;
}


void Board::UpdateFirmwareProcess( uint32_t crc )
{
	if ( mUpdating ) {
		return;
	}
	gDebug() << "Updating Flight Controller firmware\n";
	mUpdating = true;

	std::ifstream firmware( "/tmp/flight_update" );
	if ( firmware.is_open() ) {
		firmware.seekg( 0, firmware.end );
		int length = firmware.tellg();
		uint8_t* buf = new uint8_t[ length + 1 ];
		firmware.seekg( 0, firmware.beg );
		firmware.read( (char*)buf, length );
		buf[length] = 0;
		firmware.close();
		if ( crc32( buf, length ) != crc ) {
			gDebug() << "ERROR : Wrong CRC32 for firmware upload, please retry\n";
			return;
		}
		
		system( "rm -f /tmp/update.sh" );
	
		std::ofstream file( "/tmp/update.sh" );
		file << "#!/bin/bash\n\n";
		file << "service flight stop &\n";
		file << "sleep 1\n";
		file << "service flight stop &\n";
		file << "sleep 1\n";
		file << "killall -9 flight\n";
		file << "sleep 1\n";
		file << "rm /data/prog/flight\n";
		file << "cp /tmp/flight_update /data/prog/flight\n";
		file << "sleep 2\n";
		file << "rm /tmp/flight_update\n";
		file << "sleep 1\n";
		file << "chmod +x /data/prog/flight\n";
		file << "sleep 1\n";
		file << "service flight start\n";
		file.close();

		system( "nohup sh /tmp/update.sh &" );
	}
}


void Board::Reset()
{
	gDebug() << "Restarting Flight Controller service\n";
	system( "rm -f /tmp/reset.sh" );

	std::ofstream file( "/tmp/reset.sh" );
	file << "#!/bin/bash\n\n";
	file << "service flight stop &\n";
	file << "sleep 1\n";
	file << "service flight stop &\n";
	file << "sleep 1\n";
	file << "killall -9 flight\n";
	file << "service flight start\n";
	file.close();

	system( "sh /tmp/reset.sh" );
}


static std::string readproc( const std::string& filename, const std::string& entry = "", const std::string& delim = ":" )
{
	char buf[1024] = "";
	std::string res = "";
	std::ifstream file( filename );

	if ( file.is_open() ) {
		if ( entry.length() == 0 or entry == "" ) {
			file.readsome( buf, sizeof( buf ) );
			res = buf;
		} else {
			while ( file.good() ) {
				file.getline( buf, sizeof(buf), '\n' );
				if ( strstr( buf, entry.c_str() ) ) {
					char* s = strstr( buf, delim.c_str() ) + delim.length();
					while ( *s == ' ' or *s == '\t' ) {
						s++;
					}
					char* end = s;
					while ( *end != '\n' and *end++ );
					*end = 0;
					res = std::string( s );
					break;
				}
			}
		}
		file.close();
	}

	return res;
}


std::string Board::readcmd( const std::string& cmd, const std::string& entry, const std::string& delim )
{
	char buf[1024] = "";
	std::string res = "";
	FILE* fp = popen( cmd.c_str(), "r" );
	if ( !fp ) {
		printf( "popen failed : %s\n", strerror( errno ) );
		return "";
	}

	if ( entry.length() == 0 or entry == "" ) {
		fread( buf, 1, sizeof( buf ), fp );
		res = buf;
	} else {
		while ( fgets( buf, sizeof(buf), fp ) ) {
			if ( strstr( buf, entry.c_str() ) ) {
				char* s = strstr( buf, delim.c_str() ) + delim.length();
				while ( *s == ' ' or *s == '\t' ) {
					s++;
				}
				char* end = s;
				while ( *end != '\n' and *end++ );
				*end = 0;
				res = std::string( s );
				break;
			}
		}
	}

	pclose( fp );
	return res;
}


std::string Board::infos()
{
	std::string res = "";

	res += "Type:" BOARD "\n";
	res += "Firmware version:" VERSION_STRING "\n";
	res += "Model:" + readproc( "/proc/cpuinfo", "Hardware" ) + std::string( "\n" );
	res += "CPU:" + readproc( "/proc/cpuinfo", "model name" ) + std::string( "\n" );
	res += "CPU cores count:" + std::to_string( sysconf( _SC_NPROCESSORS_ONLN ) ) + std::string( "\n" );
	res += "CPU frequency:" + std::to_string( std::atoi( readproc( "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq" ).c_str() ) / 1000 ) + std::string( "MHz\n" );
	res += "GPU frequency:" + std::to_string( std::atoi( readcmd( "vcgencmd measure_clock core", "frequency", "=" ).c_str() ) / 1000000 ) + std::string( "MHz\n" );
	res += "SoC voltage:" + readcmd( "vcgencmd measure_volts core", "volt", "=" ) + std::string( "\n" );
	res += "RAM:" + readcmd( "vcgencmd get_mem arm", "arm", "=" ) + std::string( "\n" );
	res += "VRAM:" + readcmd( "vcgencmd get_mem gpu", "gpu", "=" ) + std::string( "\n" );

	return res;
}


void Board::InformLoading( int force_led )
{
	static uint64_t ticks = 0;
	static bool led_state = true;
	char cmd[256] = "";

	if ( force_led == 0 or force_led == 1 or GetTicks() - ticks >= 1000 * 250 ) {
		ticks = GetTicks();
		led_state = !led_state;
		if ( force_led == 0 or force_led == 1 ) {
			led_state = force_led;
		}
// 		sprintf( cmd, "echo %d > /sys/class/leds/led1/brightness", led_state );
		sprintf( cmd, "echo %d > /sys/class/leds/led0/brightness", led_state );
		system( cmd );
	}
}


void Board::LoadingDone()
{
	InformLoading( true );
}


void Board::setLocalTimestamp( uint32_t t )
{
	time_t tt = t;
	stime( &tt );
}


Board::Date Board::localDate()
{
	Date ret;
	struct tm* tm = nullptr;
	time_t tmstmp = time( nullptr );

	tm = localtime( &tmstmp );
	ret.year = 1900 + tm->tm_year;
	ret.month = tm->tm_mon;
	ret.day = tm->tm_mday;
	ret.hour = tm->tm_hour;
	ret.minute = tm->tm_min;
	ret.second = tm->tm_sec;

	return ret;
}


const std::string Board::LoadRegister( const std::string& name )
{
	if ( mRegisters.find( name ) != mRegisters.end() ) {
		return mRegisters[ name ];
	}
	return "";
}


int Board::SaveRegister( const std::string& name, const std::string& value )
{
	mRegisters[ name ] = value;

	std::ofstream file( "/data/flight_regs" );
	if ( file.is_open() ) {
		for ( auto reg : mRegisters ) {
			std::string line = reg.first + "=" + reg.second + "\n";
			file.write( line.c_str(), line.length() );
		}
		file.flush();
		file.close();
		sync();
		return 0;
	}

	return -1;
}


uint64_t Board::GetTicks()
{
	if ( mTicksBase == 0 ) {
		struct timespec now;
		clock_gettime( CLOCK_MONOTONIC, &now );
		mTicksBase = (uint64_t)now.tv_sec * 1000000ULL + (uint64_t)now.tv_nsec / 1000ULL;
	}

	struct timespec now;
	clock_gettime( CLOCK_MONOTONIC, &now );
	return (uint64_t)now.tv_sec * 1000000ULL + (uint64_t)now.tv_nsec / 1000ULL - mTicksBase;
}


uint64_t Board::WaitTick( uint64_t ticks_p_second, uint64_t lastTick, uint64_t sleep_bias )
{
	uint64_t ticks = GetTicks();

	if ( ( ticks - lastTick ) < ticks_p_second ) {
		int64_t t = (int64_t)ticks_p_second - ( (int64_t)ticks - (int64_t)lastTick ) + sleep_bias;
		if ( t < 0 ) {
			t = 0;
		}
		usleep( t );
	}

	return GetTicks();
}


void Board::VCOSInit()
{
	VCHIQ_INSTANCE_T vchiq_instance;
	int success = -1;
	char response[ 128 ];

	vcos_init();

	if ( vchiq_initialise( &vchiq_instance ) != VCHIQ_SUCCESS ) {
		gDebug() << "* Failed to open vchiq instance\n";
		exit(-1);
	}

	gDebug() << "vchi_initialise\n";
	success = vchi_initialise( &global_initialise_instance );
	vcos_assert(success == 0);
	vchiq_instance = (VCHIQ_INSTANCE_T)global_initialise_instance;

	global_connection = vchi_create_connection( single_get_func_table(), vchi_mphi_message_driver_func_table() );

	gDebug() << "vchi_connect\n";
	vchi_connect( &global_connection, 1, global_initialise_instance );

	vc_vchi_gencmd_init( global_initialise_instance, &global_connection, 1 );
	vc_vchi_dispmanx_init( global_initialise_instance, &global_connection, 1 );
	vc_vchi_tv_init( global_initialise_instance, &global_connection, 1 );
	vc_vchi_cec_init( global_initialise_instance, &global_connection, 1 );

	if ( success == 0 ) {
		success = vc_gencmd( response, sizeof(response), "set_vll_dir /sd/vlls" );
		vcos_assert( success == 0 );
	}
}


uint32_t Board::CPULoad()
{
	uint32_t jiffies[7];
	std::stringstream ss;
	ss.str( readcmd( "cat /proc/stat | grep \"cpu \" | cut -d' ' -f2-", "", "" ) ); 

	ss >> jiffies[0];
	ss >> jiffies[1];
	ss >> jiffies[2];
	ss >> jiffies[3];
	ss >> jiffies[4];
	ss >> jiffies[5];
	ss >> jiffies[6];

	uint64_t work_jiffies = jiffies[0] + jiffies[1] + jiffies[2];
	uint64_t total_jiffies = jiffies[0] + jiffies[1] + jiffies[2] + jiffies[3] + jiffies[4] + jiffies[5] + jiffies[6];

	uint32_t ret = ( work_jiffies - mLastWorkJiffies ) * 100 / ( total_jiffies - mLastTotalJiffies );

	mLastWorkJiffies = work_jiffies;
	mLastTotalJiffies = total_jiffies;
	return ret;
}


uint32_t Board::CPUTemp()
{
	return std::atoi( readcmd( "vcgencmd measure_temp", "temp", "=" ).c_str() );
}
