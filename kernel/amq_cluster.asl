<?xml version="1.0"?>
<protocol>

<class
    name    = "cluster"
    handler = "cluster"
    index   = "61440"
  >
  methods for intracluster communications

<doc>
  The cluster methods are used by peers in a cluster.
</doc>

<doc name = "rule">
  Standard clients MUST NOT use the cluster class, which is reserved
  for AMQP servers participating in cluster communications.
</doc>

<doc name = "grammar">
    cluster             = C:ROOT
                        / C:BIND
                        / C:STATUS
</doc>

<chassis name = "server" implement = "MAY" />
<chassis name = "client" implement = "MAY" />

<!-- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->

<method name = "root">
enable or disable server as root
  <doc>
    This method tells the cluster whether the specified server is
    taking or dropping the 'root' role.  For a full explanation of
    the AMQ cluster design please refer to the AMQ Cluster design
    documentation and specifications.
  </doc>
  <chassis name = "server" implement = "MUST" />
  <chassis name = "client" implement = "MUST" />

  <field name = "active" type = "bit">
     activate or disactivate root
    <doc>
      Specifies whether the originating server is taking or dropping
      the root role.
    </doc>
  </field>
</method>

<!-- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->

<method name = "bind">
  bind local exchange to remote exchange
  <doc>
    This method binds an exchange on one server to an exchange on
    another server.
  </doc>
  <chassis name = "server" implement = "MUST" />
  <chassis name = "client" implement = "MUST" />

  <field name = "exchange" domain = "exchange name">
    <doc>
      The name of the remote exchange to bind to. The exchange must
      exist both on the originating server, and the target server.
      The same exchange name is used when publishing messages back
      to the originating server.
    </doc>
    <doc name = "rule">
      If the exchange does not exist the server MUST raise a channel
      exception with reply code 404 (not found).
    </doc>
  </field>

  <field name = "routing key" type = "shortstr">
     message routing key
    <doc>
      Specifies the routing key for the binding.  The routing key is
      used for routing messages depending on the exchange configuration.
      Not all exchanges use a routing key - refer to the specific exchange
      documentation.
    </doc>
  </field>

  <field name = "arguments" type = "table">
    arguments for binding
    <doc>
      A set of arguments for the binding.  The syntax and semantics of
      these arguments depends on the exchange class.
    </doc>
  </field>
</method>

<!-- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->

<method name = "status">
  provide peer status data
  <doc>
    This method provides a cluster peer with status information.  We
    use this method for cluster heartbeating and synchronisation.
  </doc>
  <chassis name = "server" implement = "MUST" />
  <chassis name = "client" implement = "MUST" />

  <field name = "heartbeat" type = "short">
    peer heartbeat interval
    <doc>
      Specifies the peer's heartbeat interval, which lets the
      recipient know when the peer has stopped responding.
    </doc>
  </field>

  <field name = "connections" type = "long">
    number of client connections
    <doc>
      Specifies the number of client connections that the peer
      has.
    </doc>
  </field>

  <field name = "messages" type = "long">
    number of held messages
    <doc>
      Specifies the number of messages that the peer currently
      holds in memory.
    </doc>
  </field>

  <field name = "allocation" type = "long">
    allocated memory
    <doc>
      Specifies the current memory consumption of the peer, in
      Megabytes.
    </doc>
  </field>

  <field name = "exchanges" type = "long">
    number of exchanges
    <doc>
      Specifies the number of exchanges that exist on the peer.
    </doc>
  </field>

  <field name = "queues" type = "long">
    number of queues
    <doc>
      Specifies the number of queues that exist on the peer.
    </doc>
  </field>

  <field name = "consumers" type = "long">
    number of consumers
    <doc>
      Specifies the number of consumers that exist on the peer.
    </doc>
  </field>

  <field name = "bindings" type = "long">
    number of bindings
    <doc>
      Specifies the number of bindings that exist on the peer.
    </doc>
  </field>

  <field name = "peers" type = "long">
    number of cluster peers
    <doc>
      Specifies the total number of cluster peers that the peer
      can see, including itself.
    </doc>
  </field>

  <field name = "primaries" type = "long">
    number of primary peers
    <doc>
      Specifies the number of primary peers that the peer can see,
      including itself if it is a primary peer.  This is used to
      detect split networks.
    </doc>
  </field>

  <field name = "primary" type = "bit">
     primnary or secondary peer?
    <doc>
      Specifies whether the sender is a primary or secondary peer.
      Note that this information should be known by the recipient;
      we restate it for sanity checking.
    </doc>
  </field>

  <field name = "root" type = "bit">
     primary root peer?
    <doc>
      Specifies whether the sender is the primary root peer. Note
      that this information should be known by the recipient; we
      restate it for sanity checking.
    </doc>
  </field>
</method>

</class>
</protocol>
