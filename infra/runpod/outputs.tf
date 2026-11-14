output "endpoint_id" {
  value = runpod_serverless_endpoint.worker.id
}

output "base_url" {
  value = runpod_serverless_endpoint.worker.request_urls.base
}
