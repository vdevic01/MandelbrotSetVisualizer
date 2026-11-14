variable "runpod_api_key" {
  description = "RunPod API key. Set it in terraform.tfvars, which is gitignored."
  type        = string
  sensitive   = true
  default     = null
}

variable "endpoint_name" {
  description = "Name of the serverless endpoint."
  type        = string
}

variable "image" {
  description = "Worker image, e.g. a public image built from Worker/Dockerfile. Only used when the endpoint is created; later changes are ignored."
  type        = string
}

variable "gpu_pools" {
  description = "Serverless GPU pools the workers may run on."
  type        = list(string)
  default     = ["AMPERE_24"]
}

variable "workers_max" {
  description = "Maximum number of workers."
  type        = number
  default     = 1
}
