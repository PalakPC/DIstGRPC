# include <iostream>
# include <memory>
# include <string>
# include <sstream>
# include <thread>
# include <fstream>
# include <chrono>

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

using namespace std;

class VendorStub {
   private:
      unique_ptr<Vendor::Stub> stub_;
   
   public:
      explicit VendorStub(shared_ptr<Channel> channel) 
         : stub_(Vendor::NewStub(channel))
      {
      }
      
      string getBidDetails(const string &product_name)
      {
         BidQuery bidQuery;
         BidReply bidReply;
         ClientContext context;
         CompletionQueue cq;
         Status status;
         void *got_tag;
         bool ok;

         bidQuery.set_product_name(product_name);

         unique_ptr<ClientAsyncResponseReader<BidReply>> rpc(stub_->AsyncgetProductBid(&context, bidQuery, &cq));

         rpc->StartCall();
         rpc->Finish(&bidReply, &status, (void *)1);

         ok = false;

         GPR_ASSERT(cq.Next(&got_tag, &ok));
         GPR_ASSERT(got_tag == (void *)1);
         GPR_ASSERT(ok);

         if (status.ok())
         {
            ostringstream price;
            price << bidReply.price();
            return "Vendor ID:\t" + bidReply.vendor_id() + "\tPrice:\t" + price.str() + "\n";
         }
         else
         {
            return "RPC failed";
         }
      }
};

string stub_call(string product_name, string ip)
{
   string stubReply;
   VendorStub vendorStub(grpc::CreateChannel(ip, grpc::InsecureChannelCredentials()));
   
   stubReply = vendorStub.getBidDetails(product_name) ;
   cout << "Bid Received - \t" << stubReply << "\n";
   return stubReply;;
}

class Store_Server final
{
   private:
      class ClientCall
      {
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
                  
                  vector<string> ip;

                  string address;
                  ifstream file("vendor_addresses.txt");
                  while (getline(file, address))
                  {
                     ip.push_back(address);
                  }

                  for(int i = 0; i < ip.size(); i++)
                  {
                     string product_name = request_.product_name();
                     string reply_c = stub_call(product_name, ip[i]);
                     ProductInfo product;
                     product.set_vendor_id(reply_c);
                     reply_.add_products()->CopyFrom(product);
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
      };
      
      unique_ptr<ServerCompletionQueue> cq_;
      Store::AsyncService service_;
      unique_ptr<Server> server_;

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

   public:
      ~Store_Server()
      {
         server_->Shutdown();
         cq_->Shutdown();
      }

      void runStore()
      {
         ServerBuilder builder;
         std::string server_address("localhost:50056");

         builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
         builder.RegisterService(&service_);
         cq_ = builder.AddCompletionQueue();
         server_ = builder.BuildAndStart();
         std::cout << "Store listening on:\t" << server_address << "\n";
         handleRpcs();
      }
};

int run_store()
{
   Store_Server server;
   server.runStore();
   return 0;
}

/*class store { 

};*/

int main(int argc, char** argv) {
   //	std::cout << "I 'm not ready yet!" << std::endl;
   //	return EXIT_SUCCESS;

   unsigned int max_threads;
   string address;
   
   if (argc == 3)
   {
      address = string(argv[1]);
      max_threads = atoi(argv[2]);
   }
   else if (argc == 2)
   {
      address = string(argv[1]);
      max_threads = 5;
   }
   else
   {
      address = "localhost:50056";
      max_threads = 5;
   }

   ThreadPool my_pool(max_threads);
   my_pool.addThread(&run_store);

   my_pool.join();

   return EXIT_SUCCESS;
}

