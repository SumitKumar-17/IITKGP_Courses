from PIL import Image
import numpy as np

def extract_bit_planes(image_path):
    # Load image and convert to numpy array
    img = Image.open(image_path).convert('RGB')
    data = np.array(img)
    
    # We will look at the Red, Green, and Blue planes
    # Usually, LSB is bit 0
    for bit in range(1, 2): # Focus on bit 0 (LSB)
        for channel_idx, channel_name in enumerate(['Red', 'Green', 'Blue']):
            # Extract the specific bit
            plane = (data[:,:,channel_idx] >> bit) & 1
            
            # Convert back to an image (0 becomes 0, 1 becomes 255)
            plane_img = Image.fromarray((plane * 255).astype(np.uint8))
            
            output_name = f"plane_{channel_name}_bit{bit}.png"
            plane_img.save(output_name)
            print(f"Saved: {output_name}")

extract_bit_planes("p3.png")