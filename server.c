#define _GNU_SOURCE
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

// Sends html to client
void send_response(int client_fd, const char *status, const char *content_type, const char *content, size_t content_length) {
   dprintf(client_fd, "HTTP/1.0 200 OK\r\n", status);
   dprintf(client_fd, "Content-Type: %s\r\n", content_type);
   dprintf(client_fd, "Content-Length: %ld\r\n\r\n", content_length);

   if (content && content_length > 0) {
      write(client_fd, content, content_length);
   }
}

// Sends error html to client
void send_error(int client_fd, const char *status, const char *error) {
   char errorMessage[512]; // Arbitrary cap
   snprintf(errorMessage, sizeof(errorMessage), "<html><body><h1>%s</h1><p>%s</p></body></html>", status, error);
   send_response(client_fd, status, "text/html", errorMessage, strlen(errorMessage));
}



void cgi_support2(char *url, FILE *network, char *type) {
    // Remove leading '/' from the URL
    if (url[0] == '/') {
        memmove(url, url + 1, strlen(url));
    }

    // Prevent directory traversal attack
    if (strstr(url, "..")) {
        send_error(fileno(network), "Error 403", "<h3>Forbidden >_<</h3><p>Permission to access the requested file was denied.</p>");
        return;
    }

    // Extract command and arguments
    char *urlCopy = strdup(url); // Copy URL to avoid modifying the original
    char *command = strtok(urlCopy + 8, "?"); // Skip "/cgi-bin/"
    char *allArgs = strtok(NULL, "");

    // Construct command path
    char command_path[512];
    snprintf(command_path, sizeof(command_path), "cgi-bin/%s", command);

    // Parse arguments
    char *argv[256];
    int argc = 0;
    argv[argc++] = command_path; // First argument is the command name

    if (allArgs) {
        char *arg = strtok(allArgs, "&");
        while (arg && argc < 255) { // Cap at 255 arguments to leave space for NULL terminator
            argv[argc++] = arg;
            arg = strtok(NULL, "&");
        }
    }
    argv[argc] = NULL; // Null terminator for execvp

    // Create a unique temporary file for output
    char tempFile[256];
    snprintf(tempFile, sizeof(tempFile), "/tmp/cgi_output_%d.txt", getpid());
    printf("\nchildOutput file name: %s", tempFile);

    // Fork the process
    pid_t pid = fork();
    if (pid < 0) {
        send_error(fileno(network), "500 Internal Server Error", "<h3>Server Error</h3><p>Unable to fork a new process.</p>");
        free(urlCopy);
        return;
    }

    if (pid == 0) {
        // Child process
        int fd = open(tempFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            char printString[512];
            snprintf(printString, sizeof(printString), "<h3>Server Error</h3><p>Command path: %s\nCommand: %s \n Argv: %s</p>", command_path, command, *argv);
            send_error(fileno(network), "500 Internal Server Error", printString);
            exit(1);
        }

        // Redirect stdout and stderr to the file
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);

        // Execute the command
        execvp(command_path, argv); //command_path

        // If execvp fails
        fprintf(stderr, "Failed to execute command: %s\n", command_path);
        exit(1);
    }

    // Parent process
    int status;
    waitpid(pid, &status, 0);

    // Open the output file
    FILE *file = fopen(tempFile, "r");
    if (!file) {
        send_error(fileno(network), "500 Internal Server Error", "<h3>Server Error</h3><p>Failed to open CGI output file.</p>");
        free(urlCopy);
        return;
    }

    // Determine the size of the output
    fseek(file, 0, SEEK_END);
    long content_length = ftell(file);
    rewind(file);

    // Read and send the file content
    char *content = malloc(content_length + 1);
    if (content) {
        fread(content, 1, content_length, file);
        content[content_length] = '\0';
        send_response(fileno(network), "200 OK", "text/html", content, content_length);
        free(content);
    } else {
        send_error(fileno(network), "500 Internal Server Error", "<h3>Server Error</h3><p>Failed to allocate memory for CGI output.</p>");
    }

    fclose(file);
    remove(tempFile); // Remove the temporary file
    free(urlCopy);
}



void cgi_support(char *url, FILE *network, char *type) {
   /*
   If the exec of the program is successful, then your valid reply will 
   need to include the size of the contents.  This size, however, cannot 
   be determined until the command completes.  As such, you should have 
   the command redirect to a file (use the child process's pid in the 
   filename to avoid conflicts), read and reply with the contents of the 
   file once the child has terminated, and remove the file after the reply 
   is complete.
   */

   // Remove leading '/'
   if (url[0] == '/') {
        memmove(url, url + 1, strlen(url)); // Used ChatGPT to help me figure out how to best remove first character
   }


   // '..' Check to prevent security issues
   if (strstr(url, "..")) {
      send_error(fileno(network), "Error 403", "<h3>Forbidden >_<</h3><p>Permission to access the requested files denied.</p>");
      return;
   }

   // Break up url - format for execution
   char *urlCopy = strdup(url); // Make a copy to prevent changing of original input
   char *command = strtok(urlCopy + 8, "?"); // Skip "/cgi-bin/"
   char *allArgs = strtok(NULL, "");
   

   // Reconstruct command path
   char command_path[512]; // Arbitrary cap
   snprintf(command_path, sizeof(command_path), "cgi-bin/%s", command);

   // Save args to a list of args
   char *argv[256];
   int argc = 0;
   argv[argc++] = command_path; // First argument is command path

   if (allArgs) {
      char *arg = strtok(allArgs, "&");
      while (arg && argc < 255) { // cap at 255, not 256 to save space for null terminator
         argv[argc++] = arg;
         arg = strtok(NULL, "&");
      }
   }
   argv[argc] = NULL;   // Null terminator
   
   // Create a unique temp file for output
   char childOutput[256];
   snprintf(childOutput, sizeof(childOutput), "/tmp/cgi_output_%d.txt", getpid()); // Make file name and save to childOutput

   // Spawn child process to execute command
   pid_t pid = fork();
   if (pid < 0) {
      send_error(fileno(network), "Error 500", "<h3>Internal error :(</h3><p>Fork failed in cgi_support.</p>");
      free(urlCopy);
      return;
   } else if (pid == 0) { 
      // Child process
      // // create file to save command output (using PID in name)
      // char childOutput[256];  // Arbitrary file name length
      // // "/cgi_output_%d.txt"

      // Open file for writing output
      int fd = open(childOutput, O_WRONLY | O_CREAT | O_TRUNC, 0644); // Used ChatGPT to help me figure out the arguments
      if (fd == -1) {
         send_error(fileno(network), "Error 500", "<h3>Internal error :(</h3><p><Could not open file for command output./p>"); // Print to network because fd failed
         exit(1);
      }

      // Redirect output to file
      dup2(fd, STDOUT_FILENO);
      close(fd); // close later?

      // Execute the command
      execvp(command, argv); //execvp(command_path, argv); 
      //printf("Failed to execute command.");
      dup2(STDOUT_FILENO, STDOUT_FILENO);

      



      ////////////////////////////////////
      // Reaching this point means "exec" failed
      // Try opening html file, if provided.
      if (strstr(command_path, ".html")) {
         // Open output file (if it exists)
         FILE *file = fopen(command_path, "r");
         if (file == NULL) {
            send_error(fileno(network), "Error 500", "<h3>Internal error :(</h3><p>File doesn't exist. Parent process cgi support</p>"); 
            exit(1);
         }

         // Determine the size of the output
         fseek(file, 0, SEEK_END);
         long content_length = ftell(file);
         rewind(file);

         // Read and send the file content
         char *content = malloc(content_length + 1);
         if (content) {
            fread(content, 1, content_length, file);
            content[content_length] = '\0';
            if (!strcmp(type, "GET")) {
               content[0] = '\0'; // Make it an empty list
            }
            send_response(fileno(network), "200 OK", "text/html", content, content_length);
            free(content);
         } else {
            send_error(fileno(network), "500 Internal Server Error", "<h3>Server Error</h3><p>Failed to allocate memory for CGI output.</p>");
         }

         fclose(file);
      
      } else {
         send_error(fileno(network), "Error 501", "<h3>Not implemented :P</h3><p>Could not execute command or open file.</p>");
      }


         // TODO: The cgi-bin directory name must immediately follow the / in order to be considered valid.
                    
      //    // Print reply (write to network)
      //    struct stat requestStat;
      //    if (stat(command_path, &requestStat) == 0 && S_ISREG(requestStat.st_mode)) {
      //       // Open provided file for reading
      //       FILE *file = fopen(command_path, "r");
      //       if (file) {
      //          // TODO: Send_reply
      //          send_response(filno(network), "200 OK", "text/html", content, content_length);
      //          if (strcmp(type, "GET") == 0) {
      //             char buffer[8192];
      //             size_t bytes;
      //             while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
      //                printf("-----%s", buffer);
      //                write(fd, buffer, bytes);
      //                //fwrite(buffer, 1, bytes, fd); // network --> fd . TODO: maybe write(network, buffer, bytes); instead???
      //             }
      //          }
      //          printf("\n~~~~~After getting reading and writing contents.");
      //          //send_error(fileno(network), "Child should be writing - done writing to file", "<h3>Why's this not working</h3><p>Used fileno(network) as output</p>");
      //          // Done with file, close it
      //          fclose(file);
      //          printf("\n~~~~~Closed file");
      //       }
      //    } else {
      //       // TODO: nfd --> network --> fd
      //       printf("\n~~~~~File not found");
      //       send_error(fd, "Error 404", "<h3>File not found -_-</h3><p>Could not open requested file.</p>");
      //       exit(1); // TODO: THESE SHOULD GIVING HTML RESPONSES!! See chat response for what each error is. (Internal server error, not found, not implemented)
      //    }
      // } else {
      //    printf("\n~~~~~Not implemented");
      //    send_error(fd, "Error 501", "<h3>Not implemented :P</h3><p>Could not execute command or open file.</p>");
      // }


      // printf("\n~~~~~Something else went wrong");
      // // TODO: test this error --> maybe don't need exit statement????
      // send_error(fd, "Error 500", "<h3>Internal error :(</h3><p>Exec failed. cgi_support --> child process</p>"); 
      // printf("\n~~~~~Right before exiting cgi support");
      // exit(1);
      ////////////////////////////////////









      // char printString[512];
      // snprintf(printString, sizeof(printString), "<h3>Internal error :(</h3><p>Command path: %s\nCommand: %s \n Argv: %s</p>", command_path, command, *argv);
      // send_error(fileno(network), "Error 500", printString); 
      
      
      // exit(1);
       
      // // TODO: Move the redirection and execution to after the html check?
      // // Redirect output to file
      // // fflush(STDOUT_FILENO);
      // // // TODO: what about this? fflush(stdout);
      // // if (dup2(fd, STDOUT_FILENO) == -1) {
      // //    printf("\n!!!!!! Inside of dup2");
      // //    // TODO: test this error --> maybe don't need exit statement????
      // //    send_error(fileno(network), "Error 500", "<h3>Internal error :(</h3><p>FILENO(NETWORK): dup2 in cgi support child process failed</p>"); 
      // //    send_error(fd, "Error 500", "<h3>Internal error :(</h3><p>dup2 in cgi support child process failed</p>"); 
      // //    exit(1);
      // // }
      // printf("\n<><> TESTING PRINTING SOMETHING, THIS SHOULD BE PUT ONTO CHILD OUTPUT FILE <><>");
      // //THIS DID NOTHING: fflush(stdout);
      // // execute command
      // execvp(command, argv); // command, argv
      // // printf("\n~~~~~After exec, so it failed");
      // // if (dup2(STDOUT_FILENO, STDOUT_FILENO) == -1) {
      // //    send_error(fd, "Error 500", "<h3>Internal error :(</h3><p>dup2 in cgi support child process failed</p>"); 
      // //    exit(1);
      // // }
      // //send_error(fd, "Child should be writing", "<h3>Why's this not working</h3><p>Used fd as output</p>");
      // //send_error(fileno(network), "Child should be writing", "<h3>Why's this not working</h3><p>Used fileno(network) as output</p>");
          
      // // Reaching this point means "exec" failed
      // // Try opening html file, if provided.
      // if (strstr(command_path, ".html")) {
      //    printf("\n~~~~~There is an html file");
      //    // TODO: The cgi-bin directory name must immediately follow the / in order to be considered valid.
      //    // Remove leading '/' from the path if it exists
      //    //if (filename[0] == '/') memmove(filename, filename + 1, strlen(filename)); // Used ChatGPT to help me figure out how to best remove first character
                    
      //    // Print reply (write to network)
      //    struct stat requestStat;
      //    //printf("%s", filename);
      //    if (stat(command_path, &requestStat) == 0 && S_ISREG(requestStat.st_mode)) {
      //       // Open provided file for reading
      //       FILE *file = fopen(command_path, "r");
      //       if (file) {
      //          printf("\n~~~~~Should be showing contents (loading them below)");
      //          // TODO: Send_reply
      //          // TODO: figure out what to send this to --> was nfd?
      //          //fprintf(fd, "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %lld\r\n\r\n", requestStat.st_size);
      //          //send_error(fileno(network), "Child should be writing  - before getting contents for first time", "<h3>Why's this not working</h3><p>Used fileno(network) as output</p>");
      //          printf("\n~~~~~About to get them\n");
      //          if (strcmp(type, "GET") == 0) {
      //             char buffer[8192];
      //             size_t bytes;
      //             while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
      //                printf("-----%s", buffer);
      //                write(fd, buffer, bytes);
      //                //fwrite(buffer, 1, bytes, fd); // network --> fd . TODO: maybe write(network, buffer, bytes); instead???
      //             }
      //          }
      //          printf("\n~~~~~After getting reading and writing contents.");
      //          //send_error(fileno(network), "Child should be writing - done writing to file", "<h3>Why's this not working</h3><p>Used fileno(network) as output</p>");
      //          // Done with file, close it
      //          fclose(file);
      //          printf("\n~~~~~Closed file");
      //       }
      //    } else {
      //       // TODO: nfd --> network --> fd
      //       printf("\n~~~~~File not found");
      //       send_error(fd, "Error 404", "<h3>File not found -_-</h3><p>Could not open requested file.</p>");
      //       exit(1); // TODO: THESE SHOULD GIVING HTML RESPONSES!! See chat response for what each error is. (Internal server error, not found, not implemented)
      //    }
      // } else {
      //    printf("\n~~~~~Not implemented");
      //    send_error(fd, "Error 501", "<h3>Not implemented :P</h3><p>Could not execute command or open file.</p>");
      // }
      // // printf("\n~~~~~Something else went wrong");
      // // // TODO: test this error --> maybe don't need exit statement????
      // // send_error(fd, "Error 500", "<h3>Internal error :(</h3><p>Exec failed. cgi_support --> child process</p>"); 
      // // printf("\n~~~~~Right before exiting cgi support");
      // // exit(1);
      
   } else if (pid > 0) {
      // Parent process
      // wait for child to finish
      int status;
      waitpid(pid, &status, 0);

      // Open output file (if it exists)
      FILE *file = fopen(childOutput, "r");
      if (file == NULL) {
         send_error(fileno(network), "Error 500", "<h3>Internal error :(</h3><p>File doesn't exist. Parent process cgi support</p>"); 
         free(urlCopy);
         return;
      }

      // Determine the size of the output
      fseek(file, 0, SEEK_END);
      long content_length = ftell(file);
      rewind(file);

      // Read and send the file content
      char *content = malloc(content_length + 1);
      if (content) {
         fread(content, 1, content_length, file);
         content[content_length] = '\0';
         send_response(fileno(network), "200 OK", "text/html", content, content_length);
         free(content);
      } else {
         send_error(fileno(network), "500 Internal Server Error", "<h3>Server Error</h3><p>Failed to allocate memory for CGI output.</p>");
      }

      fclose(file);
      remove(childOutput); // Remove the temporary file
      free(urlCopy);
   }
}

void handle_request(int nfd)
{
   FILE *network = fdopen(nfd, "r+"); //r+ so that it can read + write
   char *line = NULL;
   size_t size;
   ssize_t num;

   if (network == NULL) {
      send_error(nfd, "Error 400.3", "<h3>Bad request :/</h3><p>Could not open provided client. Closing connection.</p>"); 
      close(nfd);
      exit(1);
   }

   // Get one line of request and check that it's valid
   while ((num = getline(&line, &size, network)) >= 0) {
      printf("\n%s", line);
      // Line should be of format: TYPE filename HTTP/version
      // Split the input into variables
      char *type = strtok(line, " "); 
      char *filename = strtok(NULL, " ");
      char *version = strtok(NULL, "\r\n");
      
      printf("\nTYPE: %s, FILENAME: %s, VERSION: %s", type, filename, version);
      
      // Make sure variables are valid
      if (type == NULL || filename == NULL || version == NULL) {
         send_error(nfd, "Error 400.1", "<h3>Bad request :/</h3><p>Missing essential request information (type, filename/command, and/or version).</p>"); 
         exit(1);
      } else if (strcmp(type, "GET") != 0 && strcmp(type, "HEAD") != 0) {
         send_error(nfd, "Error 400.2", "<h3>Bad request :/</h3><p>Invalid request type. (Not GET or HEAD.)</p>"); 
         exit(1);
      } else if (strstr(filename, "/cgi-bin/") == filename) { // strstr() --> substring. Source: https://www.tutorialspoint.com/c_standard_library/c_function_strstr.htm
         cgi_support(filename, network, type);
      } else {
         // Remove leading '/' from the path if it exists
         if (filename[0] == '/') memmove(filename, filename + 1, strlen(filename)); // Used ChatGPT to help me figure out how to best remove first character

         // Print reply (write to network)
         struct stat requestStat;
         
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
            send_error(nfd, "Error 404", "<h3>File not found -_-</h3><p>Could not open requested file.</p>");
            exit(1);
         }
      }
   }

   free(line);
   fclose(network);

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

void run_service(int fd) {
   while (1) {
      int nfd = accept_connection(fd);
      if (nfd != -1) {
         printf("\nConnection established\n");

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
            // wait for child to finish before closing the connection
            int status;
            waitpid(pid, &status, 0);
         } else {
            // Fork failed
            send_error(nfd, "Error 500", "<h3>Internal error :(</h3><p>Fork failed.</p>");
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

   if (fd == -1) {
      perror(0);
      exit(1);
   }

   printf("listening on port: %d\n", PORT);
   run_service(fd);
   close(fd);

   return 0;
}







/*
GENERAL TODOs
- Valgrind report
- move this to httpd.c
- The client may close the connection while the server is processing the file. Be sure to handle this case without the server crashing.
- not giving a forbidden error...
- resolve cgi-support for html files (not commands)
- clean up comments
- make cgi_output files in /tmp/
*/
