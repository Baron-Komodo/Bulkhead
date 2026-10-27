#include "raylib.h"

int main(void)
{
    InitWindow(1280, 720, "Unit Preview");
    SetTargetFPS(60);

    // The 3D preview's resolution.
    RenderTexture2D preview = LoadRenderTexture(512, 512);

    Camera3D camera = { 0 };
    camera.position = (Vector3){ 4.0f, 3.0f, 5.0f };
    camera.target = (Vector3){ 0.0f, 0.5f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 40.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    Model unit = LoadModelFromMesh(GenMeshCube(1, 1, 1));
    float rotation = 0.0f;

    // Where the preview appears in the UI.
    Rectangle panel = { 900, 350, 300, 300 };

    while (!WindowShouldClose())
    {
        rotation += GetFrameTime() * 25.0f;

        // 1. Render the 3D model into a texture.
        BeginTextureMode(preview);
            ClearBackground((Color){ 15, 19, 28, 255 });

            BeginMode3D(camera);
                DrawGrid(10, 1.0f);

                DrawModelEx(
                    unit,
                    (Vector3){ 0, 0.5f, 0 },
                    (Vector3){ 0, 1, 0 },
                    rotation,
                    (Vector3){ 1, 1, 1 },
                    WHITE
                );
            EndMode3D();
        EndTextureMode();

        // 2. Draw your normal 2D game and UI.
        BeginDrawing();
            ClearBackground((Color){ 25, 28, 35, 255 });

            DrawRectangle(0, 0, 1280, 720,
                          (Color){ 25, 28, 35, 255 });

            DrawText("UNIT INFORMATION", 900, 310, 20, WHITE);

            // 3. Draw the 3D texture into the UI panel.
            // Render textures are vertically flipped in raylib.
            DrawTexturePro(
                preview.texture,
                (Rectangle){
                    0, 0,
                    (float)preview.texture.width,
                    (float)-preview.texture.height
                },
                panel,
                (Vector2){ 0, 0 },
                0.0f,
                WHITE
            );

            // Draw UI borders on top of the preview.
            DrawRectangleLinesEx(panel, 2, GOLD);
            DrawText("MARINE", 920, 660, 22, WHITE);

        EndDrawing();
    }

    UnloadModel(unit);
    UnloadRenderTexture(preview);
    CloseWindow();

    return 0;
}
