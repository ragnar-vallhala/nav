package auth

import (
	"fmt"
	"log"
	"net/smtp"
	"os"
)

// SendVerificationEmail dispatches a high-entropy challenge packet requiring runtime confirmation.
func SendVerificationEmail(recipientEmail, username, code string) error {
	host := os.Getenv("SMTP_HOST")
	port := os.Getenv("SMTP_PORT")
	sender := os.Getenv("SMTP_USER")
	pass := os.Getenv("GOOGLE_APP_PASSWORD")
	fromName := os.Getenv("EMAIL_FROM_NAME")

	if host == "" || sender == "" || pass == "" {
		log.Println("[MAIL_BYPASS] Missing SMTP credentials, bypassing verification delivery.")
		return nil
	}

	subject := "Subject: [Nav Registry] CONFIDENTIAL - Action Required: Verify Node Access\r\n"
	mime := "MIME-version: 1.0;\nContent-Type: text/html; charset=\"UTF-8\";\r\n"
	
	body := fmt.Sprintf(`
		<div style="font-family: monospace; background: #0d1117; color: #c9d1d9; padding: 32px; border: 1px solid #39d353; max-width: 600px;">
			<h2 style="color: #39d353;">> IDENTIFICATION_REQUIRED</h2>
			<p>Greetings node: <strong>%s</strong></p>
			<p>To unlock high-clearance capabilities, you must confirm this email channel immediately.</p>
			
			<div style="background: #161b22; border: 1px solid #30363d; padding: 24px; text-align: center; margin: 24px 0;">
				<span style="font-size: 32px; letter-spacing: 12px; font-weight: bold; color: #58a6ff;">%s</span>
			</div>

			<p style="color: #8b949e; font-size: 12px;">// SECURITY NOTE: This sequence vector will decay. Execute provision swiftly.</p>
		</div>
	`, username, code)

	msg := []byte("From: " + fromName + " <" + sender + ">\r\n" +
		"To: " + recipientEmail + "\r\n" +
		subject + mime + "\r\n" +
		body)

	auth := smtp.PlainAuth("", sender, pass, host)
	addr := fmt.Sprintf("%s:%s", host, port)

	err := smtp.SendMail(addr, auth, sender, []string{recipientEmail}, msg)
	if err != nil {
		log.Printf("[MAIL_FAULT] Failed challenge delivery to %s: %v", recipientEmail, err)
		return err
	}

	log.Printf("[MAIL_PASS] Challenge token %s successfully delivered.", code)
	return nil
}

