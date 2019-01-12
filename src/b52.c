/**
 * Compile:
 *   cc src/b52.c -o /tmp/b52 -I/usr/include/mysql/ -lmysqlclient -L/usr/lib/x86_64-linux-gnu -lcurl
 *
 * Run:
     b52 [total_requests:4] [concurrency:2]
 *
 * Debug build:
 *   cc -g -O0 src/b52.c -o /tmp/b52-dbg -I/usr/include/mysql/ -lmysqlclient -L/usr/lib/x86_64-linux-gnu -lcurl
 *
 * Debugging:
 *   valgrind --leak-check=yes /tmp/b52-dbg 2
 *   scan-build-6.0 cc -g -O0 src/b52.c -o /tmp/b52-dbg -I/usr/include/mysql/ -lmysqlclient -L/usr/lib/x86_64-linux-gnu -lcurl
 *
 * Understanding output:
 *   - Curl Status Codes: https://curl.haxx.se/libcurl/c/libcurl-errors.html
 *
 * TODO:
 *   - reqs stats (success vs failures, response time, ...)
 *   - warn if Mysql returns fewer results than expected.
 */
#include <arpa/inet.h>
#include <curl/multi.h>
#include <locale.h>
#include <mysql.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <wchar.h>

/* ========================================
 * Configuration Section.
 * ======================================== */

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

/* ========================================
 * No changes below this line should be necessary.
 * ======================================== */

/**
 * Minimal struct shaping a node in the linked list used to store
 * the URLs.
 */
struct url_list {
  struct url_list *next;
  wchar_t url[URL_LEN];
};

struct url_list *first = NULL;

/**
 * Fetch the list of URLs from Mysql/Mariadb and populate the list.
 */
void load_urls(int c)
{
  MYSQL_STMT *stmt;
  MYSQL_BIND bind_p[1];
  MYSQL_BIND bind_r[1];
  wchar_t wurl_data[URL_LEN] = L"";
  unsigned long length[1];
  struct url_list *prev = NULL;

  MYSQL *con = mysql_init(NULL);
  if (con == NULL) {
    fprintf(stderr, "%s\n", mysql_error(con));
    mysql_library_end();
    exit(1);
  }

  if (mysql_options(con, MYSQL_SET_CHARSET_NAME, "utf8") != 0) {
    fprintf(stderr, "%s\n", mysql_error(con));
    mysql_close(con);
    mysql_library_end();
    exit(1);
  }

  if (mysql_real_connect(con, DATABASE_HOST, DATABASE_USER, DATABASE_PASSWORD,
                         NULL, 0, NULL, 0) == NULL) {
    fprintf(stderr, "%s\n", mysql_error(con));
    mysql_close(con);
    mysql_library_end();
    exit(1);
  }

  stmt = mysql_stmt_init(con);

  if (mysql_stmt_prepare(stmt, SELECT_QUERY, strlen(SELECT_QUERY))) {
    fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
    mysql_stmt_free_result(stmt);
    mysql_close(con);
    mysql_library_end();
    exit(1);
  }

  memset(bind_p, 0, sizeof(bind_p));
  bind_p[0].buffer_type=MYSQL_TYPE_LONG;
  bind_p[0].buffer=(char *)&c;
  bind_p[0].is_null= 0;
  bind_p[0].length= 0;

  if (mysql_stmt_bind_param(stmt, bind_p)) {
    fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
    mysql_stmt_free_result(stmt);
    mysql_close(con);
    mysql_library_end();
    exit(1);
  }

  if (mysql_stmt_execute(stmt)) {
    fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
    mysql_stmt_free_result(stmt);
    mysql_close(con);
    mysql_library_end();
    exit(1);
  }

  memset(bind_r, 0, sizeof(bind_r));

  bind_r[0].buffer_type= MYSQL_TYPE_STRING;
  bind_r[0].buffer= (wchar_t *)wurl_data;
  bind_r[0].buffer_length= URL_LEN;

  if (mysql_stmt_execute(stmt)) {
    fprintf(stderr, "%s\n", mysql_stmt_error(stmt));
    mysql_stmt_free_result(stmt);
    mysql_close(con);
    mysql_library_end();
    exit(1);
  }

  if (mysql_stmt_bind_result(stmt, bind_r)) {
    fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
    mysql_stmt_free_result(stmt);
    mysql_close(con);
    mysql_library_end();
    exit(1);
  }

  if (mysql_stmt_store_result(stmt)) {
    fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
    mysql_stmt_free_result(stmt);
    mysql_close(con);
    mysql_library_end();
    exit(1);
  }

  while (!mysql_stmt_fetch(stmt)) {
    struct url_list *current = (struct url_list*) malloc(sizeof(struct url_list));
    *current = (struct url_list) { NULL, 0 };
    if (prev == NULL)
      first = current;
    else
      prev->next = current;
    wcsncpy(current->url, wurl_data, wcslen(wurl_data));
    prev = current;
  }

  if (mysql_stmt_free_result(stmt) != 0) fwprintf(stderr, L"unable to free stmt");
  mysql_close(con);
  mysql_library_end();
}

/**
 * Curl write callback stub function, silences curl from sending html to
 * fwrite.
 */
size_t null_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
  return size * nmemb;
}

/**
 * Execute HTTP requests.
 * Walk the linked list to execute handlecount requests using the curl multi interface.
 * Adapted from https://curl.haxx.se/libcurl/c/multi-app.html.
 *
 * Returns: the next head of the linked list to be processed.
 */
struct url_list* reqs(struct url_list *first, int handlecount)
{
  int i;
  int http_handle[handlecount];
  for (i = 0; i < handlecount; i++)
    http_handle[i] = i;

  CURL *handles[handlecount];
  CURLM *multi_handle;

  int still_running = 0; /* keep number of running handles */

  CURLMsg *msg; /* for picking up messages with the transfer status */
  int msgs_left; /* how many messages are left */

  curl_global_init(CURL_GLOBAL_ALL);

  /* Allocate one CURL handle per transfer */
  for (i = 0; i < handlecount; i++) {
    handles[i] = curl_easy_init();
    curl_easy_setopt(handles[i], CURLOPT_URL, first->url);
    curl_easy_setopt(handles[i], CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(handles[i], CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(handles[i], CURLOPT_WRITEFUNCTION, null_write_callback);
    curl_easy_setopt(handles[i], CURLOPT_USERAGENT, "B52 Load Tester/1.0");
    first = first->next;
    if (first == NULL) {
      handlecount = i + 1;
      break;
    }
  }

  /* init a multi stack */
  multi_handle = curl_multi_init();

  /* add the individual transfers */
  for (i = 0; i < handlecount; i++)
    curl_multi_add_handle(multi_handle, handles[i]);

  /* we start some action by calling perform right away */
  curl_multi_perform(multi_handle, &still_running);

  while (still_running) {
    struct timeval timeout;
    int rc; /* select() return code */
    CURLMcode mc; /* curl_multi_fdset() return code */

    fd_set fdread;
    fd_set fdwrite;
    fd_set fdexcep;
    int maxfd = -1;

    long curl_timeo = -1;

    FD_ZERO(&fdread);
    FD_ZERO(&fdwrite);
    FD_ZERO(&fdexcep);

    /* set a suitable timeout to play around with */
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    curl_multi_timeout(multi_handle, &curl_timeo);
    if (curl_timeo >= 0) {
      timeout.tv_sec = curl_timeo / 1000;
      if (timeout.tv_sec > 1)
        timeout.tv_sec = 1;
      else
        timeout.tv_usec = (curl_timeo % 1000) * 1000;
    }

    /* get file descriptors from the transfers */
    mc = curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);

    if (mc != CURLM_OK) {
      fprintf(stderr, "curl_multi_fdset() failed, code %d.\n", mc);
      break;
    }

    /* On success the value of maxfd is guaranteed to be >= -1. We call
       select(maxfd + 1, ...); specially in case of (maxfd == -1) there are
       no fds ready yet so we call select(0, ...) --or Sleep() on Windows--
       to sleep 100ms, which is the minimum suggested value in the
       curl_multi_fdset() doc. */

    if (maxfd == -1) {
      /* Portable sleep for platforms other than Windows. */
      struct timeval wait = { 0, 100 * 1000 }; /* 100ms */
      rc = select(0, NULL, NULL, NULL, &wait);
    } else {
      /* Note that on some platforms 'timeout' may be modified by select().
         If you need access to the original value save a copy beforehand. */
      rc = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);
    }

    switch (rc) {
    case -1:
      /* select error */
      break;
    case 0: /* timeout */
    default: /* action */
      curl_multi_perform(multi_handle, &still_running);
      break;
    }
  }

  /* See how the transfers went */
  while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
    if (msg->msg == CURLMSG_DONE) {
      int idx, found = 0;

      /* Find out which handle this message is about */
      for (idx = 0; idx < handlecount; idx++) {
        found = (msg->easy_handle == handles[idx]);
        if (found) {
          wprintf(L"request %d status %d\n", idx + 1, msg->data.result);
          break;
        }
      }
    }
  }

  curl_multi_cleanup(multi_handle);

  /* Free the CURL handles */
  for (i = 0; i < handlecount; i++)
    curl_easy_cleanup(handles[i]);

  curl_global_cleanup();

  return first;
}

int main(int argc, char **argv)
{
  struct timeval tval_before, tval_after, tval_result;
  gettimeofday(&tval_before, NULL);

  int i, c, concurrency;
  struct url_list *ptr = NULL;

  setlocale(LC_ALL, "");
  setlocale(LC_CTYPE, "");

  c = 4;
  if (argc > 1)
    c = atoi(argv[1]);

  concurrency = 2;
  if (argc > 2)
    concurrency = atoi(argv[2]);

  load_urls(c);
  ptr = first;
  while (ptr != NULL) {
    wprintf(L"========================================\n");
    wprintf(L"Executing new batch of %d requests.\n", concurrency);
    ptr = reqs(ptr, concurrency);
  }

  // Cleanup.
  ptr = first;
  while (ptr != NULL) {
    first = ptr->next;
    free(ptr);
    ptr = first;
  }

  gettimeofday(&tval_after, NULL);
  timersub(&tval_after, &tval_before, &tval_result);
  wprintf(L"========================================\n");
  wprintf(L"All done in %ld.%04ld seconds.\n", (long int)tval_result.tv_sec, (long int)tval_result.tv_usec);

  return 0;
}
