resource "aws_instance" "web" {
  ami = "abc"
  tags = {
    Name = "web"
  }
  ports = [
    80,
    443,
  ]
  user = lookup(
    var.users,
    "web",
  )
}

locals {
  names = [for s in var.list : upper(s)]
  map = {
    for k, v in var.m : k => v
  }
}
