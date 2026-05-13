export type UploadUrlRequest = {
  filename: string;
  content_type: string;
};

export type UploadUrlResponse = {
  upload_url: string;
  public_url: string;
  object_key: string;
  bucket: string;
  content_type: string;
  expires_in: number;
};

export type DownloadUrlResponse = {
  download_url: string;
  public_url: string;
  object_key: string;
  bucket: string;
  expires_in: number;
};
