# TempoQuadruped

A simple Unreal content plugin demonstrating how to use the [Locomotor](https://dev.epicgames.com/community/learning/tutorials/EkxO/unreal-engine-procedural-animation-with-a-locomotor) plugin, new in UE 5.6, to procedurally animate a quadruped robot. Enjoy! Need some help? Find the authors on [![Discord](https://img.shields.io/badge/Discord-Join%20Server-5865F2?logo=discord&logoColor=white)](https://discord.gg/bKa2hnGYnw)

https://github.com/user-attachments/assets/2ccde47d-799f-4835-af32-8124732d353f

The Unitree Go2 model in this plugin was adapted from Christophe Yamahata's [Robot dog Unitree Go2](https://www.fab.com/listings/ec13616a-bc74-40b8-81df-a8e6c62d3e6f)

The scene in the video above was adapted from Quixel's [Derelict Corridor Megascans Sample](https://www.fab.com/listings/d825ff9c-77da-45a4-9619-c989e6dfdda6) and not included in the plugin.

The animation is driven by a control rig using Locomotor and Full Body IK nodes:
<img width="1065" height="1152" alt="Screenshot 2025-08-07 at 9 19 55 PM" src="https://github.com/user-attachments/assets/2f2f20c6-8a75-4653-ac12-1a4b3cacd691" />

The movement and control uses a standard character movement component as a "target" for the control rig:
<img width="1542" height="894" alt="Screenshot 2025-08-07 at 9 23 46 PM" src="https://github.com/user-attachments/assets/f38b34ab-de40-4193-b3df-71255c8fe7ed" />
