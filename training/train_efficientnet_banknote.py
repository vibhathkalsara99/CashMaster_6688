# Import necessary libraries
import tensorflow as tf
from tensorflow.keras.applications import EfficientNetB0
from tensorflow.keras.layers import Dense, GlobalAveragePooling2D
from tensorflow.keras.models import Model
from tensorflow.keras.preprocessing.image import ImageDataGenerator
from tensorflow.keras.callbacks import EarlyStopping
import numpy as np
import os
import shutil

# Mount Google Drive
from google.colab import drive
drive.mount('/content/drive')

# Define constants
dataset_path = '/content/drive/MyDrive/dataset'
if not os.path.exists(dataset_path):
    print(f"Directory {dataset_path} not found. Using /content/dataset instead.")
    dataset_path = '/content/dataset'
    if not os.path.exists(dataset_path):
        raise FileNotFoundError("Dataset not found at specified paths. Upload to /content/dataset or correct drive path.")

# Remove .ipynb_checkpoints if present
if os.path.exists(os.path.join(dataset_path, '.ipynb_checkpoints')):
    shutil.rmtree(os.path.join(dataset_path, '.ipynb_checkpoints'))
    print("Removed .ipynb_checkpoints folder.")

img_height = 224
img_width = 224
batch_size = 32
num_classes = 5

# Data augmentation and preprocessing
datagen = ImageDataGenerator(
    rescale=1./255,
    rotation_range=40,
    width_shift_range=0.2,
    height_shift_range=0.2,
    shear_range=0.2,
    zoom_range=0.2,
    horizontal_flip=True,
    fill_mode='nearest',
    validation_split=0.2
)

# Load and prepare data
train_generator = datagen.flow_from_directory(
    dataset_path,
    target_size=(img_height, img_width),
    batch_size=batch_size,
    class_mode='categorical',
    subset='training'
)

validation_generator = datagen.flow_from_directory(
    dataset_path,
    target_size=(img_height, img_width),
    batch_size=batch_size,
    class_mode='categorical',
    subset='validation'
)

# Verify class distribution
print("Training samples per class:")
for class_name in train_generator.class_indices:
    class_path = os.path.join(dataset_path, class_name)
    if os.path.isdir(class_path):
        count = len([f for f in os.listdir(class_path) if f.endswith(('.jpg', '.jpeg', '.png'))])
        print(f"{class_name}: {count} images")
print(f"Total training images: {train_generator.samples}")
print(f"Total validation images: {validation_generator.samples}")

# Load EfficientNetB0 as base model
base_model = EfficientNetB0(weights='imagenet', include_top=False, input_shape=(img_height, img_width, 3))

# Freeze most layers initially
base_model.trainable = False

# Add custom layers
x = base_model.output
x = GlobalAveragePooling2D()(x)
x = Dense(128, activation='relu')(x)
predictions = Dense(num_classes, activation='softmax')(x)

# Create the full model
model = Model(inputs=base_model.input, outputs=predictions)

# Compile the model
model.compile(optimizer=tf.keras.optimizers.Adam(learning_rate=0.001),
              loss='categorical_crossentropy',
              metrics=['accuracy'])

# Train the model with initial frozen layers
initial_epochs = 5
history = model.fit(
    train_generator,
    steps_per_epoch=train_generator.samples // batch_size * 2,  # Double steps to repeat data
    epochs=initial_epochs,
    validation_data=validation_generator,
    validation_steps=validation_generator.samples // batch_size
)

# Unfreeze some layers for fine-tuning
base_model.trainable = True
fine_tune_at = 150
for layer in base_model.layers[:fine_tune_at]:
    layer.trainable = False

# Recompile with adjusted learning rate
model.compile(optimizer=tf.keras.optimizers.Adam(learning_rate=1e-5),
              loss='categorical_crossentropy',
              metrics=['accuracy'])

# Fine-tune the model
fine_tune_epochs = 10
total_epochs = initial_epochs + fine_tune_epochs
early_stopping = EarlyStopping(monitor='val_loss', patience=5, restore_best_weights=True)

history_fine = model.fit(
    train_generator,
    steps_per_epoch=train_generator.samples // batch_size * 2,  # Double steps to repeat data
    epochs=total_epochs,
    initial_epoch=history.epoch[-1] + 1,
    validation_data=validation_generator,
    validation_steps=validation_generator.samples // batch_size,
    callbacks=[early_stopping]
)

# Evaluate the model
loss, accuracy = model.evaluate(validation_generator)
print(f"Validation accuracy: {accuracy:.2f}")
print(f"Validation loss: {loss:.2f}")

# Convert to TFLite
converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS]
converter._experimental_lower_tensor_list_ops = False
tflite_model = converter.convert()

# Save and download the model
with open('note_model_fixed_efficientnet.tflite', 'wb') as f:
    f.write(tflite_model)
from google.colab import files
files.download('note_model_fixed_efficientnet.tflite')

# Optional: Plot training history
import matplotlib.pyplot as plt

acc = history.history['accuracy'] + history_fine.history['accuracy']
val_acc = history.history['val_accuracy'] + history_fine.history['val_accuracy']
loss = history.history['loss'] + history_fine.history['loss']
val_loss = history.history['val_loss'] + history_fine.history['val_loss']

plt.figure(figsize=(12, 4))
plt.subplot(1, 2, 1)
plt.plot(acc, label='Training Accuracy')
plt.plot(val_acc, label='Validation Accuracy')
plt.title('Training and Validation Accuracy')
plt.legend()
plt.subplot(1, 2, 2)
plt.plot(loss, label='Training Loss')
plt.plot(val_loss, label='Validation Loss')
plt.title('Training and Validation Loss')
plt.legend()
plt.show()