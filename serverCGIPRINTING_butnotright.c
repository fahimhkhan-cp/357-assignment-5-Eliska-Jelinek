#define _GNU_SOURCE
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

void cgi_support(char *url, FILE *network) {
   printf("\nInside CGI-SUPPORT");
   // TODO: The cgi-bin directory name must immediately follow the / in order to be considered valid.

   /*
   If the exec of the program is successful, then your valid reply will 
   need to include the size of the contents.  This size, however, cannot 
   be determined until the command completes.  As such, you should have 
   the command redirect to a file (use the child process's pid in the 
   filename to avoid conflicts), read and reply with the contents of the 
   file once the child has terminated, and remove the file after the reply 
   is complete.
   */

   // TODO: Figure out if I need the below code - REWRITE MY WAY
   if (url[0] == '/') {
        memmove(url, url + 1, strlen(url));
   }


   // '..' Check to prevent security issues
   if (strstr(url, "..")) {
      fprintf(network, "HTTP/1.0 403 Forbidden\r\n\r\n");
      return;
   }

   // Spawn child process to execute command
   char *urlCopy = strdup(url); // Make a copy to prevent changing of original input
   char *command = strtok(urlCopy + 8, "?"); // Skip "/cgi-bin/"
   char *allArgs = strtok(NULL, "");

   // Save args to a list of args
   char *argv[256]; // realloc?
   int argc = 0;
   argv[argc] = command; // First argument is command name
   argc++;
   if (allArgs) {
      char *arg = strtok(allArgs, "&");
      while (arg && argc < 255) { // cap at 255, not 256 to save space for null terminator
         argv[argc] = arg;
         argc++;
         arg = strtok(NULL, "&");
      }
   }
   argv[argc] = NULL;   // Null terminator
   
   // Reconstruct command path
   char command_path[512]; // Arbitrary cap
   snprintf(command_path, sizeof(command_path), "cgi-bin/%s", command); // "/cgi-bin/%s"
   printf("\nCOMMAND: %s,  COMMAND_PATH: %s   allARGS: %s", command, command_path, allArgs);
   
   
   // Fork process to execute command
   pid_t pid = fork();
   if (pid < 0) {
      perror("fork in cgi support. 500");
      free(urlCopy); // DOES ANYTHING ELSE NEED FREEING?
      // TODO: print error
      return;
   } else if (pid == 0) {
      // Child process
      // create file to save command output (using PID in name)
      char childOutput[256];  // Arbitrary file name length
      snprintf(childOutput, sizeof(childOutput), "cgi_output_%d.txt", getpid()); // Make file name and save to childOutput
      // "/cgi_output_%d.txt"

      // Open file for writing output
      int fd = open(childOutput, O_WRONLY | O_CREAT | O_TRUNC, 0644); // TODO: rewrite this using stuff I know
      if (fd == -1) {
         // TODO: error
         perror("open in cgi support child process failed");
         exit(1);
      }
      // Redirect output to file
      if (dup2(fd, STDOUT_FILENO) == -1) {
         // TODO: error
         perror("dup2 in cgi support child process failed");
         exit(1);
      }

      /////// TESTING BASIC FILE OPENING //////
      // Print reply (write to network)
      struct stat requestStat;
      //printf("%s", filename);
      if (stat(command_path, &requestStat) == 0 && S_ISREG(requestStat.st_mode)) {
         // Open provided file for reading
         FILE *file = fopen(command_path, "r");
         if (file) {
            fprintf(network, "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %lld\r\n\r\n", requestStat.st_size);
            
            char buffer[8192]; // TODO: figure out if it should smaller/bigger/realloc
            size_t bytes;
            while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
               fwrite(buffer, 1, bytes, network);
            }
            
            // Done with file, close it
            fclose(file);
         }
      } else {
         perror("Something else went wrong. File not found.");
         exit(1); // TODO: THESE SHOULD BE GIVING HTML RESPONSES!! See chat response for what each error is. (Internal server error, not found, not implemented)
      }
      ///////////////////////


      // execute command
      execvp(command, argv);
      
      // Reaching this point means "exec" failed
      // TODO: error
      perror("Exec failed. cgi_support --> child process");
      exit(1);
      
   } else if (pid > 0) {
      // Parent process
      // wait for child to finish
      int status;
      waitpid(pid, &status, 0);

      // Open file (first, generate its name)
      char childOutput[256];
      snprintf(childOutput, sizeof(childOutput), "/cgi_output_%d.txt", pid);

      // Open file (if it exists)
      FILE *file = fopen(childOutput, "r");
      if (file == NULL) {
         // TODO: error
         perror("File doesn't exist. Parent process cgi support");
         return;
      }

      // Read contents from file
      fseek(file, 0, SEEK_END);
      long content_length = ftell(file);
      fseek(file, 0, SEEK_SET);
      // TODO: no idea what chat did here

      // Reply
      fprintf(network, "HTTP/1.0 200 OK\r\n");
      fprintf(network, "Content-Type: text/html\r\n");
      fprintf(network, "Content-Length: %ld\r\n\r\n", content_length);
      
      // Send the file contents
        char buffer[8192]; // Arbitrary amount
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
            fwrite(buffer, 1, bytes_read, network); //TODO: does this need to have "\r\n" at end?
        }

        // Close and remove file
        fclose(file);
        remove(childOutput); // Deleting file: https://www.tutorialspoint.com/c_standard_library/c_function_remove.htm
   }
}

void handle_request(int nfd)
{
   FILE *network = fdopen(nfd, "r+"); // r --> r+ so that it can read + write
   char *line = NULL;
   size_t size;
   ssize_t num;

   if (network == NULL) {
      perror("fdopen");
      close(nfd);
      return;
   }

   // Get one line of request and check that it's valid
   while ((num = getline(&line, &size, network)) >= 0) {
      printf("\nINSIDE HANDLE REQUEST ON SERVER");
      printf("\n%s", line);
      // SHOULD BE ONE LINE:
      //    TYPE filename HTTP/version
      // Split the input into variables
      char *type = strtok(line, " "); // TODO: maybe add %20 as delimiter for web browser client requests
      char *filename = strtok(NULL, " ");
      char *version = strtok(NULL, "\r\n"); // TODO: potentially different delimeter
      //filename = strstr(filename, type)+2; // Remove the type from filename (for some reason filename is the whole request still) // +2 because %20
      //filename[strlen(filename)-11] = '\0'; // len(%20HTTP/1.1) = 11
      printf("\nTYPE: %s, FILENAME: %s, VERSION: %s", type, filename, version);
      
      // Make sure variables are valid
      if (type == NULL || filename == NULL || version == NULL) {
         // TODO: ChatGPT did this differently: fprintf(network, "%s\r\n%s\r\n\r\n", HTTP_BAD_REQUEST, "Bad Request");
         perror("bad request 1");
         exit(1);
      } else if (strcmp(type, "GET") != 0 && strcmp(type, "HEAD") != 0) {
         perror("bad request 2"); // TODO
         exit(1);
      } else if (strstr(filename, "/cgi-bin/") == filename) { // strstr() --> substring. Source: https://www.tutorialspoint.com/c_standard_library/c_function_strstr.htm
      // TODO (if filename (url) has cgi-like? cgi-bin? in it)
         cgi_support(filename, network);

      } else {
         // TODO: figure out if this is needed (from chat) - OR REWRITE USING WHAT I ACTUALLY KNOW
         // Remove leading '/' from the path if it exists
         if (filename[0] == '/') memmove(filename, filename + 1, strlen(filename));

         // Print reply (write to network)
         struct stat requestStat;
         //printf("%s", filename);
         if (stat(filename, &requestStat) == 0 && S_ISREG(requestStat.st_mode)) {
            // Open provided file for reading
            FILE *file = fopen(filename, "r");
            if (file) {
               fprintf(network, "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %lld\r\n\r\n", requestStat.st_size);
               if (strcmp(type, "GET") == 0) {
                  char buffer[8192]; // TODO: figure out if it should smaller/bigger/realloc
                  size_t bytes;
                  while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
                     fwrite(buffer, 1, bytes, network);
                  }
               }
               // Done with file, close it
               fclose(file);
            }
         } else {
            perror("Something else went wrong. File not found.");
            exit(1); // TODO: THESE SHOULD GIVING HTML RESPONSES!! See chat response for what each error is. (Internal server error, not found, not implemented)
         }
      }
      

      // Process request --> GET vs HEAD (split line into vars --> strtok)
      // Print output: 
      //    Content type: text/html for everything
      //    Content length: use stat system call
      //       HTTP/1.0 200 OK\r\n
      //       Content-Type: text/html\r\n
      //       Content-Length: 5686\r\n
      //       \r\n
      // If GET:
      //    print contents too
      // If cgi-bin in url:
      //    cgi_support(url)
   }

   free(line);
   fclose(network);
}

void run_service(int fd)
{
   while (1)
   {
      int nfd = accept_connection(fd);
      if (nfd != -1)
      {
         // TODO: Get rid of these print statements? After confirming functionality?
         printf("\nConnection established\n");
         //handle_request(nfd);
         //printf("\nConnection closed\n");

         // TODO: This part - figure out if that's really where I'm supposed to be closing stuff
         pid_t pid = fork();
         if (pid == 0) {
            // Child process
            close(fd);
            handle_request(nfd);
            close(nfd);
            exit(0);
         } else if (pid > 0) {
            // Parent process
            close(nfd);
            // TODO: Close fd?
            // TODO: Wait for child processes via signal handler
         } else {
            // Fork failed
            perror("Fork failed. Exiting.");
            exit(1);
         }
         printf("\nConnection closed\n");
      }
   }
}

int main(int argc, char* argv[]) {
    //take one command-line argument specifying the port on which to listen for connections
    if (argc > 2) {
        perror("Too many arguments provided. Exiting program.");
        exit(1);
    }

    int PORT = atoi(argv[1]);
    if (PORT < 1024 || PORT > 65535) {
        perror("Please provide an integer between 1024 and 65535 (includsive). Exiting program.");
        exit(1);
    }

   int fd = create_service(PORT);

   if (fd == -1)
   {
      perror(0);
      exit(1);
   }

   // TODO: Delete this print statement?
   printf("listening on port: %d\n", PORT);
   run_service(fd);
   close(fd);

   return 0;
}







/*
GENERAL TODOs
- Makefile
- Valgrind report
- move this to httpd.c
- test
- "additional details"
- cgi-like support
-    // TODO: Though your server will handle only a single request per child process, it should not close the socket until the client does.
      // TODO: Handle erroneous requests (REQ 4)
*/
