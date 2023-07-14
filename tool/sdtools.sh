#!/bin/bash

# [] : test命令，可以用于字符串的比较，整数的比较，文件的比较，文件的判断
# $# ： 执行脚本文件时，参数的个数之和
# -eq : 整数是否相等的判断

# 移动文件
read -p "是否需要移动y/n? " chosecp
if [ $chosecp == "y" ]
then
	
	echo "*****************开始移动****************************"
	cp /home/ming/u-boot-2021.07/u-boot-spl.stm32 ./
	cp /home/ming/u-boot-2021.07/u-boot.img ./
	echo "*****************移动完成********************************"

elif [ $chosecp == "n" ]
then
	echo "*****************跳过移动********************************"
else
	echo "选择错误，请重试..."
	exit 1
fi

# 判断输入
if [ $# -eq 1 ]
then
	# == ：字符串的判断是否相等
	# $1 : 执行脚本文件时传递的第一个参数
	if [ $1 == "help" ]
	then
		echo "usage: ./sdtools.sh /dev/sdb(blockdevice)"
		exit 1
	# -b : 判断第一个参数是否为块设备文件
	elif [ ! -b $1 ]
	then
		echo "device $1 :It's not a block device"
		exit 1
	fi
else 
	echo "usage: ./sdtools.sh /dev/sdb(blockdevice)"
	exit 1
fi

# 从终端读取字符串到变量中
read -p "是否需要重新分区y/n? " chose
if [ $chose == "y" ]
then
	echo "*****************开始重新分区****************************"
	# 取消原有分区的挂载
	for (( i=1; i <=5; i++)) 
	do
		sudo umount ${1}${i}
	done
	# 删除块设备文件的原有的所有的分区
	sudo parted -s $1  mklabel msdos
	# 执行重新分区的命令
	sudo sgdisk --resize-table=128 -a 1 -n 1:34:545 -c 1:fsbl1 -n 2:546:1057 -c 2:fsbl2 -n 3:1058:5153 -c 3:ssbl -n 4:5154:136225 -c 4:bootfs -n 5:136226 -c 5:rootfs -A 4:set:2 -p $1 -g
	# sgdisk ---> 重新分区的命令
	# --resize-table=128 -a 1   --> 分区对齐
	# -n 1:34:545 -c 1:fsbl1 
	#   ---> -n(重新分区的参数)  分区编号:起始块号:终止块号 -c(分区名)  分区编号:分区名字
	# -n 2:546:1057 -c 2:fsbl2 
	# -n 3:1058:5153 -c 3:ssbl 
	# -n 4:5154:136225 -c 4:bootfs 
	# -n 5:136226 -c 5:rootfs 
	#   ---> -n(重新分区的参数)  分区编号:起始块号(剩余空间都属于这个分区) -c(分区名)  分区编号:分区名字
	# -A 4:set:2    ---> 设置分区的属性
	# -p $1 -g		---> 打印分区的结果信息

	# 关于更多sgdisk命令的使用可以man sgdisk或者sgdisk --help查看帮助手册
	echo "*****************分区完成********************************"
elif [ $chose == "n" ]
then
	echo "*****************跳过分区********************************"
else
	echo "选择错误，请重试..."
	exit 1
fi

echo "选择烧写模式"
echo "0.basic非安全烧写"
echo "1.trusted安全烧写"
read -p "请输入你的选择> " which
if [ $which -eq 0 ]
then
	echo "****************开始basic烧写****************************"
	# dd : 烧写的命令
	# if : 指定烧写的输入的文件
	# of : 指定烧写的输出的文件
	# conv=fdatasync ： 将数据所有烧写的数据读到缓冲区之后，一次烧写到输出文件中

	# sudo dd if=u-boot-spl.stm32 of=/dev/sdb1 conv=fdatasync
	sudo dd if=u-boot-spl.stm32 of="$11" conv=fdatasync

	# sudo dd if=u-boot-spl.stm32 of=/dev/sdb2 conv=fdatasync
	sudo dd if=u-boot-spl.stm32 of="$12" conv=fdatasync

	# sudo dd if=u-boot.img of=/dev/sdb3 conv=fdatasync
	sudo dd if=u-boot.img  of="$13" conv=fdatasync
	echo "****************烧写完成*********************************"
elif [ $which -eq 1 ]
then 
	echo "****************开始trusted烧写**************************"
	sudo dd if=tf-a-stm32mp157a-fsmp1a-trusted.stm32 of="$11" conv=fdatasync
	sudo dd if=tf-a-stm32mp157a-fsmp1a-trusted.stm32 of="$12" conv=fdatasync
	sudo dd if=u-boot.stm32  of="$13" conv=fdatasync
	echo "****************烧写完成*********************************"
else
	echo "选择错误，退出，请重试......."
	exit 1
fi
