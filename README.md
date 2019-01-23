# b52
Script used to load test a website by generating first a number of valid URLs for it and then fetch them.

It lets you configure the number of URLs/Requests and the concurrency level of the requests. It does very little else.

The script is extremely opinionated and covers just my personal needs (Linux, MySQL/MariaDB, wchar_t urls, libcurl, ...).

## Why

I've been an happy user of [Apache Benchamrk](https://httpd.apache.org/docs/current/programs/ab.html) and [jMeter](https://jmeter.apache.org/) for many years and found them invaluable tools to stress test my servers, but every now and then I needed to test the load of a server by GETting huge amounts of different pages and these tools don't make that super easy (think about checking how the cache behaves when traffic spikes on lots of different URLs that are not in cache).

After hacking toghether bash pipes and the like for some time, I then formalized my testing in this simple and definitely not production grade script. Use it at your own peril.

## How

Configure the sources for your case. At the top of the script src/b52.c there are few things to set:

```c
// Customize URL length and query.
#define URL_LEN 261 * sizeof(wchar_t) + 1

// Query needs to return URLs in a column called 'url', it needs to be a full URL (i.e.
// can be passed to CURL for fetching straight away). The query gets passed a LIMIT value
// coming from the script invocation arguments.
#define SELECT_QUERY "SELECT sumthin as url FROM table LIMIT ?"

// Self explanatory.
#define DATABASE_HOST "yourhost"
#define DATABASE_USER "youruser"
#define DATABASE_PASSWORD "yourpassword"
```

then you can use the Makefile to build a docker ubuntu image with all the dependencies and run the program from there:

```shell
make run-docker
make run-b52 REQS=100 CONC=5
```

REQS is the total number of requests AND urls that will be generated, CONC is the concurrency level (using libcurl multi API).

If you prefer to build it locally:

```shell
cc src/b52.c -o b52 -I/usr/include/mysql/ -lmysqlclient -L/usr/lib/x86_64-linux-gnu -lcurl
```

# Example Output

```shell
~/svn/trunk/tools/b52$ make run-b52 REQS=50 CONC=10
docker exec -u 0 -it `docker ps | grep b52-load-tester | awk '{print $1;}'` /b52 40 10
========================================
Executing new batch of 10 requests.
request 1 - curl status 0 - http response code 301
request 3 - curl status 0 - http response code 301
request 4 - curl status 0 - http response code 301
request 2 - curl status 0 - http response code 301
request 5 - curl status 0 - http response code 301
request 9 - curl status 0 - http response code 301
request 6 - curl status 0 - http response code 301
request 7 - curl status 0 - http response code 301
request 8 - curl status 0 - http response code 301
request 10 - curl status 0 - http response code 301
========================================
Executing new batch of 10 requests.
request 1 - curl status 0 - http response code 301
request 7 - curl status 0 - http response code 301
request 5 - curl status 0 - http response code 429
request 6 - curl status 0 - http response code 429
request 8 - curl status 0 - http response code 429
request 4 - curl status 0 - http response code 429
request 3 - curl status 0 - http response code 429
request 2 - curl status 0 - http response code 429
request 9 - curl status 0 - http response code 429
request 10 - curl status 0 - http response code 429
========================================
Executing new batch of 10 requests.
request 5 - curl status 0 - http response code 301
request 3 - curl status 0 - http response code 301
request 7 - curl status 0 - http response code 301
request 8 - curl status 0 - http response code 301
request 6 - curl status 0 - http response code 301
request 1 - curl status 0 - http response code 301
request 2 - curl status 0 - http response code 301
request 4 - curl status 0 - http response code 301
request 9 - curl status 0 - http response code 301
request 10 - curl status 0 - http response code 301
========================================
Executing new batch of 10 requests.
request 1 - curl status 0 - http response code 301
request 7 - curl status 0 - http response code 301
request 3 - curl status 0 - http response code 429
request 4 - curl status 0 - http response code 429
request 2 - curl status 0 - http response code 429
request 6 - curl status 0 - http response code 429
request 8 - curl status 0 - http response code 429
request 5 - curl status 0 - http response code 429
request 9 - curl status 0 - http response code 429
request 10 - curl status 0 - http response code 429
========================================
=== All done in 13.216249 seconds.
=== Total requests:     40
=== Total errors:       16 (40.00%)
=== Avg resp time:      0.019768
========================================
```

# Next

There is a TODO in the source code.

PR are welcome, also I don't do C so any improvement suggestion is gladly appreciated.
