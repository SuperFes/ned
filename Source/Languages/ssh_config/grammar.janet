# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "ssh_config"
 :extras []
 :conflicts [[_file_string _string] [_file_string _plain_string]]
 :precedences []
 :externals []
 :inline [_add_keys_to_agent_arg
          _control_master_arg
          _control_persist_arg
          _forward_agent_arg
          _identity_agent_arg
          _ipqos_arg
          _obscure_keystroke_timing_arg
          _proxy_command_arg
          _request_tty_arg
          _security_key_provider_arg
          _strict_host_key_checking_arg
          _tunnel_arg
          _tunnel_device_arg]
 :supertypes []
 :rules
 {config (:repeat
          (:choice
           (:seq (:choice _space :blank) (:choice comment :blank) _eol)
           (:seq (:choice _space :blank) host_declaration)
           (:seq (:choice _space :blank) match_declaration)
           (:seq (:choice _space :blank) parameter (:choice _space :blank) _eol)))
  host_declaration (:prec-right 0
                    (:seq
                     (:field :keyword (:alias (:pattern "Host" "i") "Host"))
                     _sep
                     (:prec-right 0
                      (:seq
                       (:field :argument (:seq (:choice "!" :blank) _pattern))
                       (:repeat
                        (:seq _space (:field :argument (:seq (:choice "!" :blank) _pattern))))))
                     (:choice _space :blank)
                     _eol
                     _declarations))
  match_declaration (:prec-right 0
                     (:seq
                      (:field :keyword (:alias (:pattern "Match" "i") "Match"))
                      _sep
                      (:choice
                       _all
                       (:prec-right 0 (:seq condition (:repeat (:seq _space condition)))))
                      (:choice _space :blank)
                      _eol
                      (:choice _declarations :blank)))
  condition (:seq
             (:choice "!" :blank)
             (:choice
              _match_canonical
              _match_final
              _match_exec
              _match_localnetwork
              _match_host
              _match_originalhost
              _match_tagged
              _match_user
              _match_localuser))
  _all (:alias (:pattern "[aA][lL][lL]") "all")
  _match_canonical (:prec-right 0
                    (:seq
                     (:field :criteria (:alias (:pattern "canonical" "i") "canonical"))
                     (:choice (:seq _sep _all) :blank)))
  _match_final (:prec-right 0
                (:seq
                 (:field :criteria (:alias (:pattern "final" "i") "final"))
                 (:choice (:seq _sep _all) :blank)))
  _match_exec (:seq
               (:field :criteria (:alias (:pattern "exec" "i") "exec"))
               _sep
               (:field :argument
                (:choice
                 (:alias (:repeat1 (:choice (:pattern "\\S") _file_token)) string)
                 (:seq "\"" (:alias (:repeat1 (:choice _char _file_token)) string) "\""))))
  _match_localnetwork (:seq
                       (:field :criteria (:alias (:pattern "localnetwork" "i") "localnetwork"))
                       _sep
                       (:field :argument
                        (:choice
                         (:prec-right 0
                          (:seq
                           (:alias (:pattern "\\S+") string)
                           (:repeat (:seq "," (:alias (:pattern "\\S+") string)))))
                         (:seq
                          "\""
                          (:prec-right 0
                           (:seq
                            (:alias (:repeat1 _char) string)
                            (:repeat (:seq "," (:alias (:repeat1 _char) string)))))
                          "\""))))
  _match_host (:seq
               (:field :criteria (:alias (:pattern "host" "i") "host"))
               _sep
               (:field :argument _match_value))
  _match_originalhost (:seq
                       (:field :criteria (:alias (:pattern "originalhost" "i") "originalhost"))
                       _sep
                       (:field :argument _match_value))
  _match_tagged (:seq
                 (:field :criteria (:alias (:pattern "tagged" "i") "tagged"))
                 _sep
                 (:field :argument _match_value))
  _match_user (:seq
               (:field :criteria (:alias (:pattern "user" "i") "user"))
               _sep
               (:field :argument _match_value))
  _match_localuser (:seq
                    (:field :criteria (:alias (:pattern "localuser" "i") "localuser"))
                    _sep
                    (:field :argument _match_value))
  _match_value (:choice
                (:prec-right 0
                 (:seq
                  (:alias (:repeat1 (:choice "*" "?" (:pattern "\\S"))) pattern)
                  (:repeat
                   (:seq "," (:alias (:repeat1 (:choice "*" "?" (:pattern "\\S"))) pattern)))))
                (:seq
                 "\""
                 (:prec-right 0
                  (:seq
                   (:alias (:repeat1 (:choice "*" "?" _char)) pattern)
                   (:repeat (:seq "," (:alias (:repeat1 (:choice "*" "?" _char)) pattern)))))
                 "\""))
  _declarations (:prec-right 0
                 (:repeat1
                  (:seq
                   (:choice _space :blank)
                   (:choice comment parameter)
                   (:choice _space :blank)
                   _eol)))
  parameter (:choice
             _add_keys_to_agent
             _address_family
             _batch_mode
             _bind_address
             _bind_interface
             _canonical_domains
             _canonicalize_fallback_local
             _canonicalize_hostname
             _canonicalize_max_dots
             _canonicalize_permitted_cnames
             _ca_signature_algorithms
             _certificate_file
             _channel_timeout
             _check_host_ip
             _ciphers
             _clear_all_forwardings
             _compression
             _connection_attempts
             _connect_timeout
             _control_master
             _control_path
             _control_persist
             _dynamic_forward
             _enable_escape_command_line
             _enable_ssh_keysign
             _escape_char
             _exit_on_forward_failure
             _fingerprint_hash
             _fork_after_authentication
             _forward_agent
             _forward_x11
             _forward_x11_timeout
             _forward_x11_trusted
             _gateway_ports
             _global_known_hosts_file
             _gssapi_authentication
             _gssapi_delegate_credentials
             _hash_known_hosts
             _hostbased_accepted_algorithms
             _hostbased_authentication
             _host_key_algorithms
             _host_key_alias
             _hostname
             _identities_only
             _identity_agent
             _identity_file
             _ignore_unknown
             _include
             _ipqos
             _kbd_interactive_authentication
             _kbd_interactive_devices
             _kex_algorithms
             _known_hosts_command
             _local_command
             _local_forward
             _log_level
             _log_verbose
             _macs
             _no_host_authentication_for_localhost
             _number_of_password_prompts
             _obscure_keystroke_timing
             _password_authentication
             _permit_local_command
             _permit_remote_open
             _pkcs11_provider
             _port
             _preferred_authentications
             _proxy_command
             _proxy_jump
             _proxy_use_fdpass
             _pubkey_accepted_algorithms
             _pubkey_authentication
             _rekey_limit
             _refuse_connection
             _remote_command
             _remote_forward
             _request_tty
             _required_rsa_size
             _revoked_host_keys
             _security_key_provider
             _send_env
             _server_alive_count_max
             _server_alive_interval
             _session_type
             _set_env
             _stdin_null
             _stream_local_bind_mask
             _stream_local_bind_unlink
             _strict_host_key_checking
             _syslog_facility
             _tcp_keep_alive
             _tag
             _tunnel
             _tunnel_device
             _update_host_keys
             _use_keychain
             _user
             _user_known_hosts_file
             _verify_host_key_dns
             _version_addendum
             _visual_host_key
             _warn_weak_crypto
             _xauth_location)
  _add_keys_to_agent (:seq
                      (:field :keyword (:alias (:pattern "AddKeysToAgent" "i") "AddKeysToAgent"))
                      _sep
                      (:field :argument _add_keys_to_agent_arg))
  _add_keys_to_agent_arg (:choice
                          _boolean
                          "ask"
                          (:prec-right 0 (:seq "confirm" (:choice (:seq _space time) :blank)))
                          time)
  _address_family (:seq
                   (:field :keyword (:alias (:pattern "AddressFamily" "i") "AddressFamily"))
                   _sep
                   (:field :argument (:choice "any" "inet" "inet6")))
  _batch_mode (:seq
               (:field :keyword (:alias (:pattern "BatchMode" "i") "BatchMode"))
               _sep
               (:field :argument _boolean))
  _bind_address (:seq
                 (:field :keyword (:alias (:pattern "BindAddress" "i") "BindAddress"))
                 _sep
                 (:field :argument _pattern))
  _bind_interface (:seq
                   (:field :keyword (:alias (:pattern "BindInterface" "i") "BindInterface"))
                   _sep
                   (:field :argument _pattern))
  _canonical_domains (:seq
                      (:field :keyword
                       (:alias (:pattern "CanonicalDomains" "i") "CanonicalDomains"))
                      _sep
                      (:prec-right 0
                       (:seq
                        (:field :argument _pattern)
                        (:repeat (:seq _space (:field :argument _pattern))))))
  _canonicalize_fallback_local (:seq
                                (:field :keyword
                                 (:alias
                                  (:pattern "CanonicalizeFallbackLocal" "i")
                                  "CanonicalizeFallbackLocal"))
                                _sep
                                (:field :argument _boolean))
  _canonicalize_hostname (:seq
                          (:field :keyword
                           (:alias (:pattern "CanonicalizeHostname" "i") "CanonicalizeHostname"))
                          _sep
                          (:field :argument (:choice "always" _boolean)))
  _canonicalize_max_dots (:seq
                          (:field :keyword
                           (:alias (:pattern "CanonicalizeMaxDots" "i") "CanonicalizeMaxDots"))
                          _sep
                          (:field :argument number))
  _canonicalize_permitted_cnames (:seq
                                  (:field :keyword
                                   (:alias
                                    (:pattern "CanonicalizePermittedCNAMEs" "i")
                                    "CanonicalizePermittedCNAMEs"))
                                  _sep
                                  (:choice
                                   "none"
                                   (:prec-right 0
                                    (:seq
                                     (:field :argument _cnames_map)
                                     (:repeat (:seq _space (:field :argument _cnames_map)))))))
  _cnames_map (:seq
               (:field :source_domain_list
                (:prec-right 0 (:seq _pattern (:repeat (:seq "," _pattern)))))
               ":"
               (:field :target_domain_list
                (:prec-right 0 (:seq _pattern (:repeat (:seq "," _pattern))))))
  _ca_signature_algorithms (:seq
                            (:field :keyword
                             (:alias (:pattern "CASignatureAlgorithms" "i") "CASignatureAlgorithms"))
                            _sep
                            (:field :argument
                             (:seq
                              (:choice (:choice "+" "-") :blank)
                              (:prec-right 0 (:seq sig (:repeat (:seq "," sig)))))))
  _certificate_file (:seq
                     (:field :keyword (:alias (:pattern "CertificateFile" "i") "CertificateFile"))
                     _sep
                     (:field :argument _file_pattern_vars))
  _channel_timeout (:seq
                    (:field :keyword (:alias (:pattern "ChannelTimeout" "i") "ChannelTimeout"))
                    _sep
                    (:prec-right 0
                     (:seq
                      (:field :argument _channel_timeout_value)
                      (:repeat (:seq _space (:field :argument _channel_timeout_value))))))
  _channel_timeout_value (:seq
                          (:choice
                           "global"
                           "agent-connection"
                           "direct-tcpip"
                           "direct-streamlocal@openssh.com"
                           "forwarded-tcpip"
                           "forwarded-streamlocal@openssh.com"
                           "session"
                           "tun-connection"
                           "x11-connection"
                           "*")
                          "="
                          time)
  _check_host_ip (:seq
                  (:field :keyword (:alias (:pattern "CheckHostIP" "i") "CheckHostIP"))
                  _sep
                  (:field :argument _boolean))
  _ciphers (:seq
            (:field :keyword (:alias (:pattern "Ciphers" "i") "Ciphers"))
            _sep
            (:field :argument
             (:seq
              (:choice (:choice "+" "-" "^") :blank)
              (:prec-right 0 (:seq cipher (:repeat (:seq "," cipher)))))))
  _clear_all_forwardings (:seq
                          (:field :keyword
                           (:alias (:pattern "ClearAllForwardings" "i") "ClearAllForwardings"))
                          _sep
                          (:field :argument _boolean))
  _compression (:seq
                (:field :keyword (:alias (:pattern "Compression" "i") "Compression"))
                _sep
                (:field :argument _boolean))
  _connection_attempts (:seq
                        (:field :keyword
                         (:alias (:pattern "ConnectionAttempts" "i") "ConnectionAttempts"))
                        _sep
                        (:field :argument number))
  _connect_timeout (:seq
                    (:field :keyword (:alias (:pattern "ConnectTimeout" "i") "ConnectTimeout"))
                    _sep
                    (:field :argument number))
  _control_master (:seq
                   (:field :keyword (:alias (:pattern "ControlMaster" "i") "ControlMaster"))
                   _sep
                   (:field :argument _control_master_arg))
  _control_master_arg (:choice _boolean "ask" "auto" "autoask")
  _control_path (:seq
                 (:field :keyword (:alias (:pattern "ControlPath" "i") "ControlPath"))
                 _sep
                 (:field :argument _file_pattern_vars))
  _control_persist (:seq
                    (:field :keyword (:alias (:pattern "ControlPersist" "i") "ControlPersist"))
                    _sep
                    (:field :argument _control_persist_arg))
  _control_persist_arg (:choice _boolean time)
  _dynamic_forward (:seq
                    (:field :keyword (:alias (:pattern "DynamicForward" "i") "DynamicForward"))
                    _sep
                    (:prec-right 0
                     (:seq
                      (:field :argument _dynamic_forward_value)
                      (:repeat (:seq _space (:field :argument _dynamic_forward_value))))))
  _forward_value_inner (:seq (:field :bind_address (:choice "*" _string)) ":" (:field :port number))
  _dynamic_forward_value (:choice
                          (:field :port number)
                          _forward_value_inner
                          (:seq "\"" _forward_value_inner "\""))
  _enable_escape_command_line (:seq
                               (:field :keyword
                                (:alias
                                 (:pattern "EnableEscapeCommandline" "i")
                                 "EnableEscapeCommandline"))
                               _sep
                               (:field :argument _boolean))
  _enable_ssh_keysign (:seq
                       (:field :keyword
                        (:alias (:pattern "EnableSSHKeysign" "i") "EnableSSHKeysign"))
                       _sep
                       (:field :argument _boolean))
  _escape_char (:seq
                (:field :keyword (:alias (:pattern "EscapeChar" "i") "EscapeChar"))
                _sep
                (:field :argument (:choice (:pattern "\\S|\\^[A-Za-z]") "none")))
  _exit_on_forward_failure (:seq
                            (:field :keyword
                             (:alias (:pattern "ExitOnForwardFailure" "i") "ExitOnForwardFailure"))
                            _sep
                            (:field :argument _boolean))
  _fingerprint_hash (:seq
                     (:field :keyword (:alias (:pattern "FingerprintHash" "i") "FingerprintHash"))
                     _sep
                     (:field :argument (:choice "md5" "sha256")))
  _fork_after_authentication (:seq
                              (:field :keyword
                               (:alias
                                (:pattern "ForkAfterAuthentication" "i")
                                "ForkAfterAuthentication"))
                              _sep
                              (:field :argument _boolean))
  _forward_agent (:seq
                  (:field :keyword (:alias (:pattern "ForwardAgent" "i") "ForwardAgent"))
                  _sep
                  (:field :argument _forward_agent_arg))
  _forward_agent_arg (:choice _boolean _string _var_value)
  _forward_x11 (:seq
                (:field :keyword (:alias (:pattern "ForwardX11" "i") "ForwardX11"))
                _sep
                (:field :argument _boolean))
  _forward_x11_timeout (:seq
                        (:field :keyword
                         (:alias (:pattern "ForwardX11Timeout" "i") "ForwardX11Timeout"))
                        _sep
                        (:field :argument time))
  _forward_x11_trusted (:seq
                        (:field :keyword
                         (:alias (:pattern "ForwardX11Trusted" "i") "ForwardX11Trusted"))
                        _sep
                        (:field :argument _boolean))
  _gateway_ports (:seq
                  (:field :keyword (:alias (:pattern "GatewayPorts" "i") "GatewayPorts"))
                  _sep
                  (:field :argument _boolean))
  _global_known_hosts_file (:seq
                            (:field :keyword
                             (:alias (:pattern "GlobalKnownHostsFile" "i") "GlobalKnownHostsFile"))
                            _sep
                            (:prec-right 0
                             (:seq
                              (:field :argument _string)
                              (:repeat (:seq _space (:field :argument _string))))))
  _gssapi_authentication (:seq
                          (:field :keyword
                           (:alias (:pattern "GSSAPIAuthentication" "i") "GSSAPIAuthentication"))
                          _sep
                          (:field :argument _boolean))
  _gssapi_delegate_credentials (:seq
                                (:field :keyword
                                 (:alias
                                  (:pattern "GSSAPIDelegateCredentials" "i")
                                  "GSSAPIDelegateCredentials"))
                                _sep
                                (:field :argument _boolean))
  _hash_known_hosts (:seq
                     (:field :keyword (:alias (:pattern "HashKnownHosts" "i") "HashKnownHosts"))
                     _sep
                     (:field :argument _boolean))
  _hostbased_accepted_algorithms (:seq
                                  (:choice
                                   (:field :keyword
                                    (:alias
                                     (:pattern "HostbasedAcceptedAlgorithms" "i")
                                     "HostbasedAcceptedAlgorithms"))
                                   (:field :keyword
                                    (:alias (:pattern "HostbasedKeyTypes" "i") "HostbasedKeyTypes")))
                                  _sep
                                  (:field :argument
                                   (:seq
                                    (:choice (:choice "+" "-" "^") :blank)
                                    (:prec-right 0 (:seq key_sig (:repeat (:seq "," key_sig)))))))
  _hostbased_authentication (:seq
                             (:field :keyword
                              (:alias
                               (:pattern "HostbasedAuthentication" "i")
                               "HostbasedAuthentication"))
                             _sep
                             (:field :argument _boolean))
  _host_key_algorithms (:seq
                        (:field :keyword
                         (:alias (:pattern "HostKeyAlgorithms" "i") "HostKeyAlgorithms"))
                        _sep
                        (:field :argument
                         (:seq
                          (:choice (:choice "+" "-" "^") :blank)
                          (:prec-right 0 (:seq key_sig (:repeat (:seq "," key_sig)))))))
  _host_key_alias (:seq
                   (:field :keyword (:alias (:pattern "HostKeyAlias" "i") "HostKeyAlias"))
                   _sep
                   (:field :argument _string))
  _hostname (:seq
             (:field :keyword (:alias (:pattern "Hostname" "i") "Hostname"))
             _sep
             (:field :argument _hostname_string))
  _identities_only (:seq
                    (:field :keyword (:alias (:pattern "IdentitiesOnly" "i") "IdentitiesOnly"))
                    _sep
                    (:field :argument _boolean))
  _identity_agent (:seq
                   (:field :keyword (:alias (:pattern "IdentityAgent" "i") "IdentityAgent"))
                   _sep
                   (:field :argument _identity_agent_arg))
  _identity_agent_arg (:choice "none" "SSH_AUTH_SOCK" _file_string _var_value)
  _identity_file (:seq
                  (:field :keyword (:alias (:pattern "IdentityFile" "i") "IdentityFile"))
                  _sep
                  (:field :argument _file_string))
  _ignore_unknown (:seq
                   (:field :keyword (:alias (:pattern "IgnoreUnknown" "i") "IgnoreUnknown"))
                   _sep
                   (:field :argument
                    (:prec-right 0 (:seq _pattern (:repeat (:seq _space _pattern))))))
  _include (:seq
            (:field :keyword (:alias (:pattern "Include" "i") "Include"))
            _sep
            (:field :argument _pattern))
  _ipqos (:prec-right 0
          (:seq
           (:field :keyword (:alias (:pattern "IPQoS" "i") "IPQoS"))
           _sep
           (:field :argument _ipqos_arg)
           (:choice (:seq _space (:field :argument _ipqos_arg)) :blank)))
  _ipqos_arg (:choice ipqos number "none")
  _kbd_interactive_authentication (:seq
                                   (:choice
                                    (:field :keyword
                                     (:alias
                                      (:pattern "KbdInteractiveAuthentication" "i")
                                      "KbdInteractiveAuthentication"))
                                    (:field :keyword
                                     (:alias
                                      (:pattern "ChallengeResponseAuthentication" "i")
                                      "ChallengeResponseAuthentication")))
                                   _sep
                                   (:field :argument _boolean))
  _kbd_interactive_devices (:seq
                            (:field :keyword
                             (:alias (:pattern "KbdInteractiveDevices" "i") "KbdInteractiveDevices"))
                            _sep
                            (:field :argument
                             (:prec-right 0 (:seq _plain_string (:repeat (:seq "," _plain_string))))))
  _kex_algorithms (:seq
                   (:field :keyword (:alias (:pattern "KexAlgorithms" "i") "KexAlgorithms"))
                   _sep
                   (:field :argument
                    (:seq
                     (:choice (:choice "+" "-" "^") :blank)
                     (:prec-right 0 (:seq kex (:repeat (:seq "," kex)))))))
  _known_hosts_command (:seq
                        (:field :keyword
                         (:alias (:pattern "KnownHostsCommand" "i") "KnownHostsCommand"))
                        _sep
                        (:field :argument _hosts_string))
  _local_command (:seq
                  (:field :keyword (:alias (:pattern "LocalCommand" "i") "LocalCommand"))
                  _sep
                  (:field :argument _token_string))
  _local_forward (:seq
                  (:field :keyword (:alias (:pattern "LocalForward" "i") "LocalForward"))
                  _sep
                  (:field :argument _forward_value1)
                  _space
                  (:field :argument _forward_value2))
  _forward_value1 (:choice
                   (:field :socket _file_string)
                   (:field :port number)
                   _forward_value_inner
                   (:seq "\"" _forward_value_inner "\""))
  _forward_value2 (:choice
                   (:field :socket _file_string)
                   _forward_value_inner
                   (:seq "\"" _forward_value_inner "\""))
  _log_level (:seq
              (:field :keyword (:alias (:pattern "LogLevel" "i") "LogLevel"))
              _sep
              (:field :argument verbosity))
  _log_verbose (:seq
                (:field :keyword (:alias (:pattern "LogVerbose" "i") "LogVerbose"))
                _sep
                (:field :argument
                 (:choice
                  (:prec-right 0 (:seq _log_verbose_value (:repeat (:seq "," _log_verbose_value))))
                  (:seq
                   "\""
                   (:prec-right 0
                    (:seq _log_verbose_quoted (:repeat (:seq "," _log_verbose_quoted))))
                   "\""))))
  _log_verbose_value (:seq
                      (:field :file (:repeat1 (:choice "*" "?" (:pattern "S"))))
                      ":"
                      (:field :function (:repeat1 (:choice "*" "?" (:pattern "S"))))
                      ":"
                      (:field :line (:choice "*" number)))
  _log_verbose_quoted (:seq
                       (:field :file (:repeat1 (:choice "*" "?" _char)))
                       ":"
                       (:field :function (:repeat1 (:choice "*" "?" _char)))
                       ":"
                       (:field :line (:choice "*" number)))
  _macs (:seq
         (:field :keyword (:alias (:pattern "MACs" "i") "MACs"))
         _sep
         (:field :argument
          (:seq
           (:choice (:choice "+" "-" "^") :blank)
           (:prec-right 0 (:seq mac (:repeat (:seq "," mac)))))))
  _no_host_authentication_for_localhost (:seq
                                         (:field :keyword
                                          (:alias
                                           (:pattern "NoHostAuthenticationForLocalhost" "i")
                                           "NoHostAuthenticationForLocalhost"))
                                         _sep
                                         (:field :argument _boolean))
  _number_of_password_prompts (:seq
                               (:field :keyword
                                (:alias
                                 (:pattern "NumberOfPasswordPrompts" "i")
                                 "NumberOfPasswordPrompts"))
                               _sep
                               (:field :argument number))
  _obscure_keystroke_timing (:seq
                             (:field :keyword
                              (:alias
                               (:pattern "ObscureKeystrokeTiming" "i")
                               "ObscureKeystrokeTiming"))
                             _sep
                             (:field :argument _obscure_keystroke_timing_arg))
  _obscure_keystroke_timing_arg (:choice _boolean (:seq "interval" ":" number))
  _password_authentication (:seq
                            (:field :keyword
                             (:alias
                              (:pattern "PasswordAuthentication" "i")
                              "PasswordAuthentication"))
                            _sep
                            (:field :argument _boolean))
  _permit_local_command (:seq
                         (:field :keyword
                          (:alias (:pattern "PermitLocalCommand" "i") "PermitLocalCommand"))
                         _sep
                         (:field :argument _boolean))
  _permit_remote_open (:seq
                       (:field :keyword
                        (:alias (:pattern "PermitRemoteOpen" "i") "PermitRemoteOpen"))
                       _sep
                       (:field :argument
                        (:prec-right 0
                         (:seq
                          _permit_remote_open_value
                          (:repeat (:seq _space _permit_remote_open_value))))))
  _permit_remote_open_value (:choice
                             "any"
                             "none"
                             (:seq
                              (:field :host (:choice "*" (:alias (:pattern "\\S+") string)))
                              ":"
                              (:field :port (:choice "*" number)))
                             (:seq
                              "\""
                              (:field :host (:choice "*" (:alias (:repeat1 _char) string)))
                              ":"
                              (:field :port (:choice "*" number))
                              "\""))
  _pkcs11_provider (:seq
                    (:field :keyword (:alias (:pattern "PKCS11Provider" "i") "PKCS11Provider"))
                    _sep
                    (:field :argument _string))
  _port (:seq
         (:field :keyword (:alias (:pattern "Port" "i") "Port"))
         _sep
         (:field :argument number))
  _preferred_authentications (:seq
                              (:field :keyword
                               (:alias
                                (:pattern "PreferredAuthentications" "i")
                                "PreferredAuthentications"))
                              _sep
                              (:field :argument
                               (:prec-right 0
                                (:seq authentication (:repeat (:seq "," authentication))))))
  _proxy_command (:seq
                  (:field :keyword (:alias (:pattern "ProxyCommand" "i") "ProxyCommand"))
                  _sep
                  (:field :argument _proxy_command_arg))
  _proxy_command_arg (:choice "none" _proxy_string)
  _proxy_jump (:seq
               (:field :keyword (:alias (:pattern "ProxyJump" "i") "ProxyJump"))
               _sep
               (:prec-right 0 (:seq _proxy_jump_value (:repeat (:seq "," _proxy_jump_value)))))
  _proxy_jump_value (:choice
                     (:field :argument "none")
                     (:seq
                      (:choice (:seq (:field :user _plain_string) "@") :blank)
                      (:field :host _plain_string)
                      (:choice (:seq ":" (:field :port number)) :blank))
                     (:field :uri (:alias (:pattern "ssh:\\/\\/\\S+") uri)))
  _proxy_use_fdpass (:seq
                     (:field :keyword (:alias (:pattern "ProxyUseFdpass" "i") "ProxyUseFdpass"))
                     _sep
                     (:field :argument _boolean))
  _pubkey_accepted_algorithms (:seq
                               (:choice
                                (:field :keyword
                                 (:alias
                                  (:pattern "PubkeyAcceptedAlgorithms" "i")
                                  "PubkeyAcceptedAlgorithms"))
                                (:field :keyword
                                 (:alias
                                  (:pattern "PubkeyAcceptedKeyTypes" "i")
                                  "PubkeyAcceptedKeyTypes")))
                               _sep
                               (:field :argument
                                (:seq
                                 (:choice (:choice "+" "-" "^") :blank)
                                 (:prec-right 0 (:seq key_sig (:repeat (:seq "," key_sig)))))))
  _pubkey_authentication (:seq
                          (:field :keyword
                           (:alias (:pattern "PubkeyAuthentication" "i") "PubkeyAuthentication"))
                          _sep
                          (:field :argument _pubkey_authentication_arg))
  _pubkey_authentication_arg (:choice _boolean "unbound" "host-bound")
  _rekey_limit (:prec-right 0
                (:seq
                 (:field :keyword (:alias (:pattern "RekeyLimit" "i") "RekeyLimit"))
                 _sep
                 (:choice
                  (:field :argument "none")
                  (:field :argument bytes)
                  (:seq (:field :argument bytes) _space (:field :argument time)))))
  _refuse_connection (:seq
                      (:field :keyword
                       (:alias (:pattern "RefuseConnection" "i") "RefuseConnection"))
                      _sep
                      (:field :argument (:alias (:repeat1 (:pattern "[^\\r\\n]")) string)))
  _remote_command (:seq
                   (:field :keyword (:alias (:pattern "RemoteCommand" "i") "RemoteCommand"))
                   _sep
                   (:field :argument
                    (:alias (:repeat1 (:choice (:pattern "[^\\r\\n]") _file_token variable)) string)))
  _remote_forward (:prec-right 0
                   (:seq
                    (:field :keyword (:alias (:pattern "RemoteForward" "i") "RemoteForward"))
                    _sep
                    (:field :argument _forward_value1)
                    (:choice (:seq _space (:field :argument _forward_value2)) :blank)))
  _request_tty (:seq
                (:field :keyword (:alias (:pattern "RequestTTY" "i") "RequestTTY"))
                _sep
                (:field :argument _request_tty_arg))
  _request_tty_arg (:choice _boolean "force" "auto")
  _required_rsa_size (:seq
                      (:field :keyword (:alias (:pattern "RequiredRSASize" "i") "RequiredRSASize"))
                      _sep
                      (:field :argument number))
  _revoked_host_keys (:seq
                      (:field :keyword (:alias (:pattern "RevokedHostKeys" "i") "RevokedHostKeys"))
                      _sep
                      (:field :argument _file_string))
  _security_key_provider (:seq
                          (:field :keyword
                           (:alias (:pattern "SecurityKeyProvider" "i") "SecurityKeyProvider"))
                          _sep
                          (:field :argument _security_key_provider_arg))
  _security_key_provider_arg (:choice _string _var_value)
  _send_env (:seq
             (:field :keyword (:alias (:pattern "SendEnv" "i") "SendEnv"))
             _sep
             (:prec-right 0
              (:seq
               (:field :argument _send_env_value)
               (:repeat (:seq _space (:field :argument _send_env_value))))))
  _send_env_value (:seq
                   (:choice "-" :blank)
                   (:alias (:repeat1 (:choice "*" "?" (:pattern "[a-zA-Z0-9_]"))) variable))
  _server_alive_count_max (:seq
                           (:field :keyword
                            (:alias (:pattern "ServerAliveCountMax" "i") "ServerAliveCountMax"))
                           _sep
                           (:field :argument number))
  _server_alive_interval (:seq
                          (:field :keyword
                           (:alias (:pattern "ServerAliveInterval" "i") "ServerAliveInterval"))
                          _sep
                          (:field :argument number))
  _session_type (:seq
                 (:field :keyword (:alias (:pattern "SessionType" "i") "SessionType"))
                 _sep
                 (:field :argument (:choice "none" "subsystem" "default")))
  _set_env (:seq
            (:field :keyword (:alias (:pattern "SetEnv" "i") "SetEnv"))
            _sep
            (:prec-right 0
             (:seq
              (:field :argument _set_env_value)
              (:repeat (:seq _space (:field :argument _set_env_value))))))
  _set_env_value (:seq (:alias _var_name variable) "=" _string)
  _stdin_null (:seq
               (:field :keyword (:alias (:pattern "StdinNull" "i") "StdinNull"))
               _sep
               (:field :argument _boolean))
  _stream_local_bind_mask (:seq
                           (:field :keyword
                            (:alias (:pattern "StreamLocalBindMask" "i") "StreamLocalBindMask"))
                           _sep
                           (:field :argument (:alias (:pattern "0?[0-7]{3}") number)))
  _stream_local_bind_unlink (:seq
                             (:field :keyword
                              (:alias
                               (:pattern "StreamLocalBindUnlink" "i")
                               "StreamLocalBindUnlink"))
                             _sep
                             (:field :argument _boolean))
  _strict_host_key_checking (:seq
                             (:field :keyword
                              (:alias
                               (:pattern "StrictHostKeyChecking" "i")
                               "StrictHostKeyChecking"))
                             _sep
                             (:field :argument _strict_host_key_checking_arg))
  _strict_host_key_checking_arg (:choice _boolean "accept-new" "off" "ask")
  _syslog_facility (:seq
                    (:field :keyword (:alias (:pattern "SyslogFacility" "i") "SyslogFacility"))
                    _sep
                    (:field :argument facility))
  _tcp_keep_alive (:seq
                   (:field :keyword (:alias (:pattern "TCPKeepAlive" "i") "TCPKeepAlive"))
                   _sep
                   (:field :argument _boolean))
  _tag (:seq (:field :keyword (:alias (:pattern "Tag" "i") "Tag")) _sep (:field :argument _string))
  _tunnel (:seq
           (:field :keyword (:alias (:pattern "Tunnel" "i") "Tunnel"))
           _sep
           (:field :argument _tunnel_arg))
  _tunnel_arg (:choice _boolean "point-to-point" "ethernet")
  _tunnel_device (:seq
                  (:field :keyword (:alias (:pattern "TunnelDevice" "i") "TunnelDevice"))
                  _sep
                  (:field :argument _tunnel_device_arg))
  _tunnel_device_arg (:seq
                      (:field :local_tun (:choice "any" number))
                      (:choice (:seq ":" (:field :remote_tun (:choice "any" number))) :blank))
  _update_host_keys (:seq
                     (:field :keyword (:alias (:pattern "UpdateHostKeys" "i") "UpdateHostKeys"))
                     _sep
                     (:field :argument (:choice _boolean "ask")))
  _use_keychain (:seq
                 (:field :keyword (:alias (:pattern "UseKeychain" "i") "UseKeychain"))
                 _sep
                 _boolean)
  _user (:seq
         (:field :keyword (:alias (:pattern "User" "i") "User"))
         _sep
         (:field :argument _string))
  _user_known_hosts_file (:seq
                          (:field :keyword
                           (:alias (:pattern "UserKnownHostsFile" "i") "UserKnownHostsFile"))
                          _sep
                          (:prec-right 0
                           (:seq
                            (:field :argument (:choice "none" _file_string))
                            (:repeat (:seq _space (:field :argument (:choice "none" _file_string)))))))
  _verify_host_key_dns (:seq
                        (:field :keyword
                         (:alias (:pattern "VerifyHostKeyDNS" "i") "VerifyHostKeyDNS"))
                        _sep
                        (:field :argument (:choice _boolean "ask")))
  _version_addendum (:seq
                     (:field :keyword (:alias (:pattern "VersionAddendum" "i") "VersionAddendum"))
                     _sep
                     (:field :argument (:choice "none" _string)))
  _visual_host_key (:seq
                    (:field :keyword (:alias (:pattern "VisualHostKey" "i") "VisualHostKey"))
                    _sep
                    (:field :argument _boolean))
  _warn_weak_crypto (:seq
                     (:field :keyword (:alias (:pattern "WarnWeakCrypto" "i") "WarnWeakCrypto"))
                     _sep
                     (:field :argument (:choice _boolean "no-pq-kex")))
  _xauth_location (:seq
                   (:field :keyword (:alias (:pattern "XAuthLocation" "i") "XAuthLocation"))
                   _sep
                   (:field :argument _string))
  ipqos (:token
         (:choice
          "af11"
          "af12"
          "af13"
          "af21"
          "af22"
          "af23"
          "af31"
          "af32"
          "af33"
          "af41"
          "af42"
          "af43"
          "cs0"
          "cs1"
          "cs2"
          "cs3"
          "cs4"
          "cs5"
          "cs6"
          "cs7"
          "ef"
          "le"
          "lowdelay"
          "throughput"
          "reliability"))
  verbosity (:token
             (:choice "QUIET" "FATAL" "ERROR" "INFO" "VERBOSE" "DEBUG" "DEBUG1" "DEBUG2" "DEBUG3"))
  facility (:token
            (:choice
             "DAEMON"
             "USER"
             "AUTH"
             "LOCAL0"
             "LOCAL1"
             "LOCAL2"
             "LOCAL3"
             "LOCAL4"
             "LOCAL5"
             "LOCAL6"
             "LOCAL7"))
  authentication (:token
                  (:choice
                   "gssapi-with-mic"
                   "hostbased"
                   "publickey"
                   "keyboard-interactive"
                   "password"))
  cipher (:token
          (:choice
           "3des-cbc"
           "aes128-cbc"
           "aes192-cbc"
           "aes256-cbc"
           "aes128-ctr"
           "aes192-ctr"
           "aes256-ctr"
           "aes128-gcm@openssh.com"
           "aes256-gcm@openssh.com"
           "chacha20-poly1305@openssh.com"))
  kex (:token
       (:choice
        "diffie-hellman-group1-sha1"
        "diffie-hellman-group14-sha1"
        "diffie-hellman-group14-sha256"
        "diffie-hellman-group16-sha512"
        "diffie-hellman-group18-sha512"
        "diffie-hellman-group-exchange-sha1"
        "diffie-hellman-group-exchange-sha256"
        "ecdh-sha2-nistp256"
        "ecdh-sha2-nistp384"
        "ecdh-sha2-nistp521"
        "curve25519-sha256"
        "curve25519-sha256@libssh.org"
        "sntrup761x25519-sha512"
        "sntrup761x25519-sha512@openssh.com"
        "mlkem768x25519-sha256"))
  key_sig (:token
           (:choice
            "ssh-ed25519"
            "ssh-ed25519-cert-v01@openssh.com"
            "sk-ssh-ed25519@openssh.com"
            "sk-ssh-ed25519-cert-v01@openssh.com"
            "ecdsa-sha2-nistp256"
            "ecdsa-sha2-nistp256-cert-v01@openssh.com"
            "ecdsa-sha2-nistp384"
            "ecdsa-sha2-nistp384-cert-v01@openssh.com"
            "ecdsa-sha2-nistp521"
            "ecdsa-sha2-nistp521-cert-v01@openssh.com"
            "sk-ecdsa-sha2-nistp256@openssh.com"
            "sk-ecdsa-sha2-nistp256-cert-v01@openssh.com"
            "webauthn-sk-ecdsa-sha2-nistp256@openssh.com"
            "webauthn-sk-ecdsa-sha2-nistp256-cert-v01@openssh.com"
            "ssh-dss"
            "ssh-dss-cert-v01@openssh.com"
            "ssh-rsa"
            "ssh-rsa-cert-v01@openssh.com"
            "rsa-sha2-256"
            "rsa-sha2-256-cert-v01@openssh.com"
            "rsa-sha2-512"
            "rsa-sha2-512-cert-v01@openssh.com"))
  mac (:token
       (:choice
        "hmac-sha1"
        "hmac-sha1-96"
        "hmac-sha2-256"
        "hmac-sha2-512"
        "hmac-md5"
        "hmac-md5-96"
        "umac-64@openssh.com"
        "umac-128@openssh.com"
        "hmac-sha1-etm@openssh.com"
        "hmac-sha1-96-etm@openssh.com"
        "hmac-sha2-256-etm@openssh.com"
        "hmac-sha2-512-etm@openssh.com"
        "hmac-md5-etm@openssh.com"
        "hmac-md5-96-etm@openssh.com"
        "umac-64-etm@openssh.com"
        "umac-128-etm@openssh.com"))
  sig (:token
       (:choice
        "ssh-ed25519"
        "sk-ssh-ed25519@openssh.com"
        "ecdsa-sha2-nistp256"
        "ecdsa-sha2-nistp384"
        "ecdsa-sha2-nistp521"
        "sk-ecdsa-sha2-nistp256@openssh.com"
        "webauthn-sk-ecdsa-sha2-nistp256@openssh.com"
        "ssh-dss"
        "ssh-rsa"
        "rsa-sha2-256"
        "rsa-sha2-512"))
  _file_token (:alias (:pattern "%[%CdhikLlnpru]") token)
  _hosts_token (:alias (:pattern "%[%CdhikLlnprufHIKt]") token)
  _hostname_token (:alias (:pattern "%[%h]") token)
  _proxy_token (:alias (:pattern "%[%hnpr]") token)
  token (:pattern "%[%CdfHhIiKkLlnprTtu]")
  _var_value (:alias (:seq "$" (:field :name _var_name)) variable)
  _var_name (:pattern "[a-zA-Z_][a-zA-Z0-9_]*")
  variable (:seq "${" (:field :name _var_name) "}")
  _file_string (:choice
                (:alias (:repeat1 (:choice (:pattern "\\S") _file_token variable)) string)
                (:seq "\"" (:alias (:repeat1 (:choice _char _file_token variable)) string) "\""))
  _hosts_string (:alias (:repeat1 (:choice (:pattern "[^\\r\\n]") _hosts_token variable)) string)
  _hostname_string (:choice
                    (:alias (:repeat1 (:choice (:pattern "\\S") _hostname_token)) string)
                    (:seq "\"" (:alias (:repeat1 (:choice _char _hostname_token)) string) "\""))
  _proxy_string (:alias (:repeat1 (:choice (:pattern "[^\\r\\n]") _proxy_token)) string)
  _token_string (:alias (:repeat1 (:choice (:pattern "[^\\r\\n]") token)) string)
  _string (:choice _plain_string (:seq "\"" (:alias (:repeat1 _char) string) "\""))
  _plain_string (:alias (:repeat1 (:pattern "\\S")) string)
  _file_pattern (:choice
                 (:alias (:repeat1 (:choice "*" "?" (:pattern "\\S") _file_token)) pattern)
                 (:seq "\"" (:alias (:repeat1 (:choice "*" "?" _char _file_token)) pattern) "\""))
  _file_pattern_vars (:choice
                      (:alias
                       (:repeat1 (:choice "*" "?" (:pattern "\\S") variable _file_token))
                       pattern)
                      (:seq
                       "\""
                       (:alias (:repeat1 (:choice "*" "?" _char variable _file_token)) pattern)
                       "\""))
  _pattern (:choice
            (:alias (:repeat1 (:choice "*" "?" (:pattern "\\S"))) pattern)
            (:seq "\"" (:alias (:repeat1 (:choice "*" "?" _char)) pattern) "\""))
  _boolean (:choice "yes" "no")
  _number (:pattern "[1-9][0-9]*|0")
  number (:prec 1 _number)
  bytes (:seq _number (:choice (:pattern "[kmgKMG]") :blank))
  time (:choice
        _number
        (:seq _number (:pattern "[wW]"))
        (:seq (:choice (:seq _number (:pattern "[wW]")) :blank) _number (:pattern "[dD]"))
        (:seq
         (:choice (:seq _number (:pattern "[wW]")) :blank)
         (:choice (:seq _number (:pattern "[dD]")) :blank)
         _number
         (:pattern "[hH]"))
        (:seq
         (:choice (:seq _number (:pattern "[wW]")) :blank)
         (:choice (:seq _number (:pattern "[dD]")) :blank)
         (:choice (:seq _number (:pattern "[hH]")) :blank)
         _number
         (:pattern "[mM]"))
        (:seq
         (:choice (:seq _number (:pattern "[wW]")) :blank)
         (:choice (:seq _number (:pattern "[dD]")) :blank)
         (:choice (:seq _number (:pattern "[hH]")) :blank)
         (:choice (:seq _number (:pattern "[mM]")) :blank)
         _number
         (:pattern "[sS]")))
  comment (:pattern "#.*")
  _sep (:choice _space (:alias (:pattern "[ \\t]*=[ \\t]*") "="))
  _char (:pattern "[^\"]|\\\\\"")
  _space (:pattern "[ \\t]+")
  _eol (:pattern "\\r?\\n")}}
