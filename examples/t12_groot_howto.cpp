#include "behaviortree_cpp/loggers/bt_file_logger_v2.h"
#include "crossdoor_nodes.h"
#include "behaviortree_cpp/bt_factory.h"
#include "behaviortree_cpp/loggers/groot2_publisher.h"
#include "behaviortree_cpp/loggers/bt_sqlite_logger.h"
#include "behaviortree_cpp/xml_parsing.h"
#include "behaviortree_cpp/json_export.h"

/** We are using the same example in Tutorial 5,
 *  But this time we also show how to connect
 */

// A custom struct  that I want to visualize in Groot2
struct Position2D
{
  double x;
  double y;
};

// This macro will generate the code that is needed to convert
// the object to/from JSON.
// You still need to call BT::RegisterJsonDefinition<Position2D>()
// in main()
BT_JSON_CONVERTER(Position2D, pos)
{
  add_field("x", &pos.x);
  add_field("y", &pos.y);
}

// Simple Action that updates an instance of Position2D in the blackboard
class UpdatePosition : public BT::SyncActionNode
{
public:
  UpdatePosition(const std::string& name, const BT::NodeConfig& config)
    : BT::SyncActionNode(name, config)
  {}

  BT::NodeStatus tick() override
  {
    _pos.x += 0.2;
    _pos.y += 0.1;
    setOutput("pos", _pos);
    return BT::NodeStatus::SUCCESS;
  }

  static BT::PortsList providedPorts()
  {
    return { BT::OutputPort<Position2D>("pos") };
  }

private:
  Position2D _pos = { 0, 0 };
};

// clang-format off

static const char* xml_text = R"(
<root BTCPP_format="4">

  <BehaviorTree ID="MainTree">
    <Sequence>
      <Script code="door_open:=false" />
      <UpdatePosition pos="{pos_2D}" />
      <Fallback>
        <Inverter>
          <IsDoorClosed/>
        </Inverter>
        <SubTree ID="DoorClosed" _autoremap="true" door_open="{door_open}"/>
      </Fallback>
      <PassThroughDoor/>
    </Sequence>
  </BehaviorTree>

  <BehaviorTree ID="DoorClosed">
    <Fallback name="tryOpen" _onSuccess="door_open:=true">
      <OpenDoor/>
        <RetryUntilSuccessful num_attempts="5">
          <PickLock/>
        </RetryUntilSuccessful>
      <SmashDoor/>
    </Fallback>
  </BehaviorTree>

</root>
 )";

// clang-format on

int main()
{
  BT::BehaviorTreeFactory factory;

  // Nodes registration, as usual
  CrossDoor cross_door;
  cross_door.registerNodes(factory);
  factory.registerNodeType<UpdatePosition>("UpdatePosition");

  // Groot2 editor requires a model of your registered Nodes.
  // You don't need to write that by hand, it can be automatically
  // generated using the following command.
  std::string xml_models = BT::writeTreeNodesModelXML(factory);

  factory.registerBehaviorTreeFromText(xml_text);

  // Add this to allow Groot2 to visualize your custom type
  BT::RegisterJsonDefinition<Position2D>();

  auto tree = factory.createTree("MainTree");

  std::cout << "----------- XML file  ----------\n"
            << BT::WriteTreeToXML(tree, false, false)
            << "--------------------------------\n";

  // Connect the Groot2Publisher. This will allow Groot2 to
  // get the tree and poll status updates.
  const unsigned port = 1667;
  BT::Groot2Publisher publisher(tree, port);

  // Add two more loggers, to save the transitions into a file.
  // Both formats are compatible with Groot2

  // Lightweight serialization
  BT::FileLogger2 logger2(tree, "t12_logger2.btlog");
  // SQLite logger can save multiple sessions into the same database
  bool append_to_database = true;
  BT::SqliteLogger sqlite_logger(tree, "t12_sqlitelog.db3", append_to_database);

  // We can add some extra information to the SqliteLogger, for instance the value of the
  // "door_open" blackboard entry, at the end of node "tryOpen" (Fallback)

  auto sqlite_callback = [](BT::Duration timestamp, const BT::TreeNode& node,
                            BT::NodeStatus prev_status,
                            BT::NodeStatus status) -> std::string {
    if(node.name() == "tryOpen" && BT::isStatusCompleted(status))
    {
      auto is_open = BT::toStr(node.config().blackboard->get<bool>("door_open"));
      return "[tryOpen] door_open=" + is_open;
    }
    return {};
  };
  sqlite_logger.setMetadataCallback(sqlite_callback);

  while(1)
  {
    std::cout << "Start" << std::endl;
    cross_door.reset();
    tree.tickWhileRunning();
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  }

  return 0;
}
