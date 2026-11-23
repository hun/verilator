-- DESCRIPTION: Verilator: an integer literal with an exponent
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (
    a : out integer;
    b : out integer;
    c : out integer
  );
end entity t;

architecture rtl of t is
  -- An exponent without a decimal point leaves the literal an integer (IEEE 1076-2008 15.5.2)
  constant K : integer := 3e6;
  signal   s : integer := 2E+2;
begin
  a <= K;
  b <= s;
  c <= 1_5e1;
end architecture rtl;
