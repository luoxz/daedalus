#include "Daedalus.h"
#include "DebugCharacter.h"
#include "DDGameState.h"
#include "Constants.h"

#include <Utilities/UnrealBridge.h>

ADebugCharacter::ADebugCharacter(const class FPostConstructInitializeProperties& PCIP)
	: Super(PCIP), TickDeltaCount(0)
{
	auto & movement = this->CharacterMovement;
	movement->SetWalkableFloorAngle(60.0);
	movement->BrakingDecelerationFlying = 1000.0;
	movement->DefaultLandMovementMode = MOVE_Flying;
}

void ADebugCharacter::MoveForward(float amount) {
	if (Controller != NULL && FMath::Abs(amount) > utils::FLOAT_ERROR) {
		FRotator rotator = Controller->GetControlRotation();
		rotator.Pitch = 0.0;

		const FVector direction = FRotationMatrix(rotator).GetScaledAxis(EAxis::X);
		AddMovementInput(direction, amount);
	}
}

void ADebugCharacter::MoveRight(float amount) {
	if (Controller != NULL && FMath::Abs(amount) > utils::FLOAT_ERROR) {
		FRotator rotator = Controller->GetControlRotation();
		const FVector direction = FRotationMatrix(rotator).GetScaledAxis(EAxis::Y);
		AddMovementInput(direction, amount);
	}
}

void ADebugCharacter::MoveUp(float amount) {
	if (Controller != NULL && FMath::Abs(amount) > utils::FLOAT_ERROR) {
		const FVector direction(0, 0, 1.0);
		AddMovementInput(direction, amount);
	}
}

void ADebugCharacter::LookUp(float amount) {
	AddControllerPitchInput(amount);
}

void ADebugCharacter::LookRight(float amount) {
	AddControllerYawInput(amount);
}

void ADebugCharacter::BeginPlay() {
	Super::BeginPlay();

	if (GEngine)
		GEngine->AddOnScreenDebugMessage(-1, 5.0, FColor::Green, TEXT("Using debug game character."));
}

void ADebugCharacter::SetupPlayerInputComponent(class UInputComponent * InputComponent) {
	InputComponent->BindAxis("MoveForward", this, &ADebugCharacter::MoveForward);
	InputComponent->BindAxis("MoveRight", this, &ADebugCharacter::MoveRight);
	InputComponent->BindAxis("MoveUp", this, &ADebugCharacter::MoveUp);
	InputComponent->BindAxis("LookUp", this, &ADebugCharacter::LookUp);
	InputComponent->BindAxis("LookRight", this, &ADebugCharacter::LookRight);
}
