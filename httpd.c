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

   // Var to keep track of output status (child will output for html files)
   int outputStatus = 0;

   // Spawn child process to execute command
   pid_t pid = fork();
   if (pid < 0) {
      send_error(fileno(network), "Error 500", "<h3>Internal error :(</h3><p>Fork failed in cgi_support.</p>");
      free(urlCopy);
      return;
   } else if (pid == 0) { 
      // Child process

      // Open file for writing output
      int fd = open(childOutput, O_WRONLY | O_CREAT | O_TRUNC, 0644); // Used ChatGPT to help me figure out the arguments
      if (fd == -1) {
         send_error(fileno(network), "Error 500", "<h3>Internal error :(</h3><p><Could not open file for command output./p>"); // Print to network because fd failed
         exit(1);
      }

      // Redirect output to file
      dup2(fd, STDOUT_FILENO);
      close(fd);

      // Execute the command
      execvp(command, argv);

      // Redirect stdout to itself (stop writing to file)
      dup2(STDOUT_FILENO, STDOUT_FILENO);

      // Reaching this point means "exec" failed
      // Try opening html file, if provided.
      if (strstr(command_path, ".html")) {
         outputStatus = 1; // Set output status to 1 --> child is handling it

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
            // if (strcmp(type, "HEAD")) {
            //    content[0] = '\0'; // Make it an empty list for HEAD requests (contents shouldn't be printed)
            // }
            send_response(fileno(network), "200 OK", "text/html", content, content_length);
            free(content);
         } else {
            send_error(fileno(network), "500 Internal Server Error", "<h3>Server Error</h3><p>Failed to allocate memory for CGI output.</p>");
         }
         fclose(file);
      } else {
         send_error(fileno(network), "Error 501", "<h3>Not implemented :P</h3><p>Could not execute command or open file.</p>");
      }
   } else if (pid > 0) {
      // Parent process
      // wait for child to finish
      int status;
      waitpid(pid, &status, 0);

      if (outputStatus != 1) {
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
      }
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
      // Line should be of format: TYPE filename HTTP/version
      // Split the input into variables
      char *type = strtok(line, " "); 
      char *filename = strtok(NULL, " ");
      char *version = strtok(NULL, "\r\n");
      
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
                  char buffer[8192]; 
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
