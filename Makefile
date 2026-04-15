PWD := $(shell pwd)
obj-m += monitor.o

all: engine cpu_workload io_workload mem_workload monitor.ko

engine: engine.c
	gcc -Wall -pthread -o engine engine.c

cpu_workload: cpu_workload.c
	gcc -Wall -static -o cpu_workload cpu_workload.c -lm

io_workload: io_workload.c
	gcc -Wall -static -o io_workload io_workload.c

mem_workload: mem_workload.c
	gcc -Wall -static -o mem_workload mem_workload.c

monitor.ko: monitor.c
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

ci: engine cpu_workload io_workload mem_workload

clean:
	rm -f engine cpu_workload io_workload mem_workload
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
