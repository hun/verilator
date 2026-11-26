-- DESCRIPTION: Verilator: VHDL context reference with a dot but no name inside a context declaration
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A qualified context reference inside a context declaration needs a simple name
-- after each dot. The reference below ends after the dot.

context cx is
  context work.;
end context cx;

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  o <= '0';
end architecture rtl;
