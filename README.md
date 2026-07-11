# corestack-peky

Corestack but distributed, mostly so I can refresh myself on chord and raft from scratch and doing it for the first time in C.


## Behaviour
Initially, a single tetrisd will be formed and with it a chord network + node will be spawned, along with a raft node.
The single server will have itself as the successor for chord, as well as being the majority for raft so the leader as well.
Only the next server (number 2) will require to know the IP:port of the genesis server, but any server after that will only have to know of any existing live server in the network to join

To simplify the implementation, Raft will be done on tetrisd itself, replicating the state across all tetrisd instances across the network.
This allows us to simplify the process of continuity since only commited entries are displayed back to tetrisu, 
and another tetrisd instance already existing in the Raft network will be guaranteed to have at least committed entries.

Tetrisd will receive inputs from tetrisu locally and proxy the inputs to the responsible tetrisd instance, which will then update the raft leader for the inputs to be commited.

Chord will be used to direct traffic towards specific tetrisd instances, 
i.e. the tetrisd incharge of a hash in its range will be the one receiving all inputs from all tetrisd proxied inputs across the network.


### Chord example
In a 3 bit chord network -> 2^3 = 8, the keyspace exists from 0...7

Assume there are 4 tetrisd servers active
+---------+--------------------+---------+
| Node ID | Address            | Meaning |
+---------+--------------------+---------+
| 0       | 10.10.50.200:9000  | tetrisd-0 |
| 1       | 10.10.50.201:9000  | tetrisd-1 |
| 3       | 10.10.50.203:9000  | tetrisd-3 |
| 6       | 10.10.50.206:9000  | tetrisd-6 |
+---------+--------------------+---------+


The chord ring will look something like this
                 +------+
                 |  0   | 10.10.50.200:9000
                 +------+

              /            \

+------+                          +------+
|  7   |                          |  1   | 10.10.50.201:9000
+------+                          +------+

   |                                  |

+------+                            +------+
|  6   | 10.10.50.206:9000          |  2   |
+------+                            +------+

        \                           /

     +------+                   +------+
     |  4   |                   |  3   | 10.10.50.203:9000
     +------+                   +------+
              \              /
                 +------+
                 |  5   |
                 +------+


Visually the node's predecessors (anticlockwise closest live server) and successors (clockwise closest live server) are in this order
+---------+-------------+-----------+
| Node ID | Predecessor | Successor |
+---------+-------------+-----------+
| 0       | 6           | 1         |
| 1       | 0           | 3         |
| 3       | 1           | 6         |
| 6       | 3           | 0         |
+---------+-------------+-----------+

Node ownership by active nodes
+---------+-------------+----------------+
| Node ID | Owns range  | Owns keys      |
+---------+-------------+----------------+
| 0       | (6, 0]      | 7, 0           |
| 1       | (0, 1]      | 1              |
| 3       | (1, 3]      | 2, 3           |
| 6       | (3, 6]      | 4, 5, 6        |
+---------+-------------+----------------+

This means for example: hash("room/main") = 5

Key 5 belongs to node 6.
So room/main is handled by tetrisd-6 at 10.10.50.206:9000.


Individually, lets look at node 6
Node ID: 6
Address: 10.10.50.206:9000
Predecessor: 3
Successor: 0

We can calculate the starting position using n+ 2^i-1, where n=6 and i starting at 1
finger[1].start = 6 + 1 mod 8 = 7
finger[2].start = 6 + 2 mod 8 = 0
finger[3].start = 6 + 4 mod 8 = 2

+-----------+-------+----------+----------------+--------------------+
| Finger    | Start | Interval | Successor Node | Successor Address  |
+-----------+-------+----------+----------------+--------------------+
| finger[1] | 7     | [7, 0)   | 0              | 10.10.50.200:9000  |
| finger[2] | 0     | [0, 2)   | 0              | 10.10.50.200:9000  |
| finger[3] | 2     | [2, 6)   | 3              | 10.10.50.203:9000  |
+-----------+-------+----------+----------------+--------------------+




### Raft example
