/*
 * CS 6210 - Fall 2017
 * Project 3
 * Store management 
 */

# include <iostream>
# include <memory>
# include <string>
# include <thread>
# include <sstream>  //For ostringstream
# include <fstream>  //for ifstream

# include <grpc++/grpc++.h>
# include <grpc/support/log.h>

# include "vendor.grpc.pb.h"
# include "store.grpc.pb.h"

# include "threadpool.h"

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;

using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCompletionQueue;

using vendor::BidQuery;
using vendor::BidReply;
using vendor::Vendor;

using store::ProductQuery;
using store::ProductReply;
using store::ProductInfo;
using store::Store;

std::string store_address; //For storing store's address

/*
 * Client part
 */

class VendorStub {
   public:
      explicit VendorStub(std::shared_ptr<Channel> channel) 
         : stub_(Vendor::NewStub(channel))
      {
      }

      std::string getBidDetails(const std::string &product_name)
      {
         BidQuery bidQuery;
         BidReply bidReply;
         ClientContext context;
         CompletionQueue cq;
         Status status;
         void *got_tag;
         bool ok;

         bidQuery.set_product_name(product_name);
         std::unique_ptr<ClientAsyncResponseReader<BidReply>> rpc(stub_->AsyncgetProductBid(&context, bidQuery, &cq));

         rpc->Finish(&bidReply, &status, (void *)1);

         ok = false;
         GPR_ASSERT(cq.Next(&got_tag, &ok));
         GPR_ASSERT(got_tag == (void *)1);
         GPR_ASSERT(ok);

         if (status.ok())
         {
            std::ostringstream price;
            
            price << bidReply.price(); 
            return "Vendor ID:\t" + bidReply.vendor_id() + "\tProduct Name:\t" + product_name+ "\tPrice:\t" + price.str() + "\n";
         }
         else
         {
            return "RPC failed";
         }
      }

   private:
      std::unique_ptr<Vendor::Stub> stub_;
};

std::string stub_call(std::string product_name, std::string ip)
{
   std::string stubReply;

   VendorStub vendorStub(grpc::CreateChannel(ip, grpc::InsecureChannelCredentials()));
   stubReply = vendorStub.getBidDetails(product_name);
   std::cout << "Bid - \t" << stubReply << "\n";   //Return string from vendor
   return stubReply;
}

/*
 * Server part
 */

class Store_Server final
{
   public:
      ~Store_Server()
      {
         server_->Shutdown();
         cq_->Shutdown();
      }

      void runStore(std::string address)
      {
         ServerBuilder builder;
         std::string server_address(address);
         
         builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
         builder.RegisterService(&service_);
         cq_ = builder.AddCompletionQueue();
         server_ = builder.BuildAndStart();
         std::cout << "Store address:\t" << server_address << "\n";
         handleRpcs();
      }

   private:
      class ClientCall
      {
         public:
            ClientCall(Store::AsyncService *service, ServerCompletionQueue *cq) 
               : service_(service), cq_(cq), responder_(&ctx_), status_(CREATE)
            {
               proceed();
            }

            void proceed()
            {
               if (status_ == CREATE)
               {
                  status_ = PROCESS;
                  service_->RequestgetProducts(&ctx_, &request_, &responder_, cq_, cq_, this);
               }
               else if (status_ == PROCESS)
               {
                  new ClientCall (service_, cq_);
                  
                  std::vector<std::string> ip;
                  std::string address;
                  std::ifstream file("vendor_addresses.txt");

                  while (getline(file, address))
                  {
                     ip.push_back(address);
                  }

                  for(int i = 0; i < ip.size(); i++)
                  {
                     std::string product_name = request_.product_name();
                     std::string reply_c = stub_call(product_name, ip[i]); //To get vendor's reply
                     ProductInfo product;
                     product.set_vendor_id(reply_c);
                  
                     reply_.add_products()->CopyFrom(product); //Reply to client
                     reply_.products(0).vendor_id();
                     reply_.products(0).price();
                  }
                  status_ = FINISH;
                  responder_.Finish(reply_, Status::OK, this);
               }
               else
               {
                  GPR_ASSERT(status_ == FINISH);
                  delete this;
               }
            }

         private:
            Store::AsyncService *service_;
            ServerCompletionQueue *cq_;
            ServerContext ctx_;
            ProductQuery request_;
            ProductReply reply_;
            ServerAsyncResponseWriter<ProductReply> responder_;

            enum CallStatus
            {
               CREATE,
               PROCESS,
               FINISH
            };

            CallStatus status_;
      };

      void handleRpcs()
      {
         new ClientCall(&service_, cq_.get());
         void *tag;
         bool ok;

         while (1)
         {
            GPR_ASSERT(cq_->Next(&tag, &ok));
            GPR_ASSERT(ok);
            static_cast<ClientCall *>(tag)->proceed();
         }
      }

      std::unique_ptr<ServerCompletionQueue> cq_;
      Store::AsyncService service_;
      std::unique_ptr<Server> server_;
};

int run_store()
{
   Store_Server server;
   server.runStore(store_address);
   return 0;
}

/*
class store { 

};
*/

int main(int argc, char** argv) {
   //	std::cout << "I 'm not ready yet!" << std::endl;
   //	return EXIT_SUCCESS;

   unsigned int max_threads;
   //std::string address;

   if (argc == 3)
   {
      store_address = std::string(argv[1]);
      max_threads = atoi(argv[2]);
   }
   else if (argc == 2)
   {
      store_address = std::string(argv[1]);
      max_threads = 5;
   }
   else
   {
      store_address = std::string("localhost:50040");
      max_threads = 5;
   }

   threadpool my_pool(max_threads);
   my_pool.addJob(&run_store);

   my_pool.joinAll();

   return EXIT_SUCCESS;
}
