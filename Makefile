bin/gpio-watch: gpio-watch.c
	mkdir -p bin
	cc -o bin/gpio-watch gpio-watch.c
