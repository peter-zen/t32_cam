#! /usr/env bash

# This is a bash script for the purpose to easily implement the following functions:
# 1. Configurate application and bsp.
# 2. Compile application and install the programs and libraries into the bsp.
# 3. Compile bsp.

DEFAULT_SENSOR_NAME=GC4653
DEFAULT_WIFI_NAME=RTL8189FTV
DEFAULT_FLASH_SIZE=32M
DEFAULT_RTC_EXIST=YES
DEFAULT_MCU_EXIST=YES

#!/bin/bash

USR_PLATFORM_NAME=V37M
USR_SENSOR_NAME=SONYIMX335
USR_WIFI_NAME=RTL8189ES
USR_AUDIO_NAME=NAU8810
USR_FLASH_TYPE=SPINOR
USR_DDR_SIZE=DDR_2G
USER_RTMP_EN=NO
USER_4G_MODULE=EC20
USER_CONFIG_TYPE=CKV
USR_RELEASE_TYPE=FORMAL_RELEASE_VERSION
USR_FORMAL_RELEASE_REVISION=NULL
USR_FORMAL_RELEASE_BUILD_DATETIME=NULL

while true; do
    echo "Please select platform name:"
    echo "1.V37M  2.V39M  3.V35AX  4.V39AX"
    read sensor_select
	
    if [ $sensor_select == "1" ]; then
		USR_PLATFORM_NAME=V37M
        break
	elif [ $sensor_select == "2" ]; then
		USR_PLATFORM_NAME=V39M
        break
	elif [ $sensor_select == "3" ]; then
		USR_PLATFORM_NAME=V35AX
        break
	elif [ $sensor_select == "4" ]; then
		USR_PLATFORM_NAME=V39AX
        break
    else
        echo "Invalid platform name. Please try again."
    fi
done

echo "You selected $USR_PLATFORM_NAME"

while true; do
    echo "Please select sensor name:"
    echo "1.SONYIMX335  2.SONYIMX317_MIPI  3.GC4653 4.GC5623 5.GC6603 6.GC8613"
    read sensor_select
	
    if [ $sensor_select == "1" ]; then
		USR_SENSOR_NAME=SONYIMX335
        break
	elif [ $sensor_select == "2" ]; then
		USR_SENSOR_NAME=SONYIMX317_MIPI
        break
	elif [ $sensor_select == "3" ]; then
		USR_SENSOR_NAME=GC4653
        break
	elif [ $sensor_select == "4" ]; then
		USR_SENSOR_NAME=GC5623
        break
	elif [ $sensor_select == "5" ]; then
		USR_SENSOR_NAME=GC6603
        break
	elif [ $sensor_select == "6" ]; then
		USR_SENSOR_NAME=GC8613
        break
    else
        echo "Invalid sensor name. Please try again."
    fi
done

echo "You selected $USR_SENSOR_NAME"

while true; do
    echo "Please select WiFi name:"
    echo "1.RTL8189ES  2.RTL8189FTV"
    read wifi_select
    
    if [ $wifi_select == "1" ]; then
		USR_WIFI_NAME=RTL8189ES
        break
	elif [ $wifi_select == "2" ]; then
		USR_WIFI_NAME=RTL8189FTV
        break
    else
        echo "Invalid WiFi name. Please try again."
    fi
done

echo "You selected $USR_WIFI_NAME"

while true; do
    echo "Please select audio chip name:"
    echo "1.NAU8810  2.CL1009S  3.MAX98357"
    read audio_select
    
    if [ $audio_select == "1" ]; then
		USR_AUDIO_NAME=NAU8810
        break
	elif [ $audio_select == "2" ]; then
		USR_AUDIO_NAME=CL1009S
        break
	elif [ $audio_select == "3" ]; then
		USR_AUDIO_NAME=MAX98357
        break
    else
        echo "Invalid audio chip name. Please try again."
    fi
done

echo "You selected $USR_AUDIO_NAME"

while true; do
    echo "Please select flash type:"
    echo "1.SPINOR  2.SPINAND"
    read flash_select
    
    if [ $flash_select == "1" ]; then
		USR_FLASH_TYPE=SPINOR
        break
	elif [ $flash_select == "2" ]; then
		USR_FLASH_TYPE=SPINAND
        break
    else
        echo "Invalid flash type. Please try again."
    fi
done

echo "You selected $USR_FLASH_TYPE"

while true; do
    echo "Please select DDR size:"
    echo "1.1Gbit  2.2Gbit"
    read ddr_size
    
    if [ $ddr_size == "1" ]; then
		USR_DDR_SIZE=DDR_1G
        break
	elif [ $ddr_size == "2" ]; then
		USR_DDR_SIZE=DDR_2G
        break
    else
        echo "Invalid DDR size. Please try again."
    fi
done

echo "You selected $USR_DDR_SIZE"

while true; do
    echo "Please enable/disable RTMP:"
    echo "1.Enable  2.Disable"
    read rtmp_enable
    
    if [ $rtmp_enable == "1" ]; then
		USER_RTMP_EN=YES
        break
	elif [ $rtmp_enable == "2" ]; then
		USER_RTMP_EN=NO
        break
    else
        echo "Invalid value. Please try again."
    fi
done

echo "You selected $rtmp_enable"

while true; do
    echo "Please select 4G module:"
    echo "1.EC20  2.EC200  3.RG255AA  4.None"
    read usb_4g_module
    
    if [ $usb_4g_module == "1" ]; then
		USER_4G_MODULE=EC20
        break
	elif [ $usb_4g_module == "2" ]; then
		USER_4G_MODULE=EC200
        break
	elif [ $usb_4g_module == "3" ]; then
		USER_4G_MODULE=RG255AA
        break
	elif [ $usb_4g_module == "4" ]; then
		USER_4G_MODULE=NONE
        break
    else
        echo "Invalid 4G module. Please try again."
    fi
done

echo "You selected $USER_4G_MODULE"

while true; do
    echo "Please select configuration type:"
    echo "1.CKV  2.WPWS"
    read config_type
    
    if [ $config_type == "1" ]; then
		USER_CONFIG_TYPE=CKV
        break
	elif [ $config_type == "2" ]; then
		USER_CONFIG_TYPE=WPWS
        break
    else
        echo "Invalid configuration type. Please try again."
    fi
done

echo "You selected $USER_CONFIG_TYPE"

while true; do
    echo "Please select release type:"
    echo "1.Debug version  2.Formal release version"
    read release_type
    
    if [ $release_type == "1" ]; then
		USR_RELEASE_TYPE=DEBUG_VERSION
        break
	elif [ $release_type == "2" ]; then
		USR_RELEASE_TYPE=FORMAL_RELEASE_VERSION
        break
    else
        echo "Invalid release type. Please try again."
    fi
done

echo "You selected $USR_RELEASE_TYPE"

if [ $USR_RELEASE_TYPE == FORMAL_RELEASE_VERSION ]; then
	while true; do
		read -p "Please input formal release revision(e.g:V2.0.0):" release_revison
		USR_FORMAL_RELEASE_REVISION=$release_revison
		break
	done

	echo "Your release revision is $USR_FORMAL_RELEASE_REVISION"
	
	while true; do
		echo "Please input formal release datetime(e.g:230101):"
		read release_datetime
		USR_FORMAL_RELEASE_BUILD_DATETIME=$release_datetime
		break
	done

	echo "Your formal release datetime is $USR_FORMAL_RELEASE_BUILD_DATETIME"
	
	while true; do
		echo "Please select GIT operation:"
		echo "1.Create GIT TAG for this formal release and push it to remote repository"
		echo "2.Create GIT TAG for this formal release but not pushing it to remote repository"
		echo "3.Nothing to do"
		
		read git_operation
		
		if [ $git_operation == "1" ]; then
			GIT_TAG=$USR_FORMAL_RELEASE_REVISION-$USR_FORMAL_RELEASE_BUILD_DATETIME
			`git tag $GIT_TAG && git push origin $GIT_TAG`
			USR_GIT_OPERATION="Create GIT TAG:$GIT_TAG and push it to remote repository"
			break
		elif [ $git_operation == "2" ]; then
			GIT_TAG=$USR_FORMAL_RELEASE_REVISION-$USR_FORMAL_RELEASE_BUILD_DATETIME
			`git tag $GIT_TAG`
			USR_GIT_OPERATION="Create GIT TAG:$GIT_TAG, but not pushing it to remote repository"
			break
		elif [ $git_operation == "3" ]; then
			USR_GIT_OPERATION="Nothing to do"
			break
		else
			echo "Invalid GIT operation. Please try again."
		fi
	done

	echo "You selected:$USR_GIT_OPERATION"
fi

echo PLATFORM_NAME:=$USR_PLATFORM_NAME > release_config.def
echo SENSOR_NAME:=$USR_SENSOR_NAME >> release_config.def
echo WIFI_NAME:=$USR_WIFI_NAME >> release_config.def
echo AUDIO_NAME:=$USR_AUDIO_NAME >> release_config.def
echo FLASH_TYPE_NAME:=$USR_FLASH_TYPE >> release_config.def
echo DDR_SIZE:=$USR_DDR_SIZE >> release_config.def
echo RTMP_EN:=$USER_RTMP_EN >> release_config.def
echo USB_4G_MODULE:=$USER_4G_MODULE >> release_config.def
echo CONFIG_TYPE:= $USER_CONFIG_TYPE >> release_config.def
echo RELEASE_TYPE:=$USR_RELEASE_TYPE >> release_config.def
echo REVISION:=$USR_FORMAL_RELEASE_REVISION >> release_config.def
echo BUILDTIME:=$USR_FORMAL_RELEASE_BUILD_DATETIME >> release_config.def
echo GIT_COMMIT:=`git rev-parse HEAD` >> release_config.def