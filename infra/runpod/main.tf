terraform {
  required_version = ">= 1.6"

  required_providers {
    runpod = {
      source  = "runpod/runpod"
      version = "~> 2.0"
    }
  }
}

provider "runpod" {
  api_key = var.runpod_api_key
}

resource "runpod_serverless_endpoint" "worker" {
  name          = var.endpoint_name
  endpoint_type = "LOAD_BALANCER"
  image         = var.image

  gpu_pools             = var.gpu_pools
  gpu_count             = 1
  allowed_cuda_versions = []

  workers_min   = 0
  workers_max   = var.workers_max
  idle_timeout  = 60
  scaling_type  = "REQUEST_COUNT"
  scaling_value = 4

  flashboot = "FLASHBOOT"
  timeout   = 600000
  disk      = 5

  ports = ["8080/http", "8081/http"]
  env = {
    PORT        = "8080"
    PORT_HEALTH = "8080"
  }

  lifecycle {
    # The image is built and rolled out by RunPod's GitHub integration,
    # so Terraform must not revert it to the value it was created with.
    ignore_changes = [image]
  }
}
